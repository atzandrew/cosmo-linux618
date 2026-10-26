// SPDX-License-Identifier: GPL-2.0
/*
 * Power-off for the MediaTek MT6358 PMIC (Cosmo Communicator, MT6771).
 *
 * The vendor ATF ignores PSCI SYSTEM_OFF. This driver follows the vendor
 * 4.4 kernel's power-off path step by step (gemian/cosmo-linux-kernel-4.4
 * @ a741a2e: mt_power_off() -> hal_rtc_bbpu_pwdn() -> PMIC_POWER_HOLD(0);
 * receipts in cosmo-linux618 docs/poweroff.md):
 *
 *  1. prepare (sleepable): the RTC/PMIC sequence of hal_rtc_bbpu_pwdn(),
 *     every register write in the vendor order;
 *  2. power-off (IRQs off): RG_PWRHOLD = 0, which cuts power;
 *  3. if power is still on after ~1.1 s (charger present, or the cut
 *     failed): the vendor arch_reset(0, "charger") fallback, a TOPRGU
 *     software reset with WDT_MODE bit 4 ("bypass power key") cleared,
 *     so the bootloader chooses power-off charging or power-off.
 *
 * Vendor field writes are unconditional read-modify-writes
 * (pmic_config_interface()), so this driver does the same rather than
 * regmap_update_bits(), which skips unchanged writes.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

/* MT6358 RTC registers, absolute PMIC addresses (vendor mt6771 upmu_hw.h) */
#define RTC_BBPU		0x588
#define  RTC_BBPU_PWREN		BIT(0)
#define  RTC_BBPU_CLR		BIT(1)
#define  RTC_BBPU_RELOAD	BIT(5)
#define  RTC_BBPU_CBUSY		BIT(6)
#define  RTC_BBPU_KEY		(0x43 << 8)
#define RTC_AL_MASK		0x590
#define  RTC_AL_MASK_DOW	BIT(4)
#define RTC_AL_SEC		0x5a0
#define  RTC_BBPU_AUTO_PDN_SEL	BIT(6)
#define  RTC_BBPU_2SEC_EN	BIT(8)
#define RTC_AL_YEA		0x5ac
#define  RTC_K_EOSC_RSV_0	BIT(8)
#define  RTC_K_EOSC_RSV_1	BIT(9)
#define  RTC_K_EOSC_RSV_2	BIT(10)
#define RTC_OSC32CON		0x5ae
#define  RTC_EMBCK_SRC_SEL	BIT(8)
#define  RTC_OSC32CON_UNLOCK1	0x1a57
#define  RTC_OSC32CON_UNLOCK2	0x2b68
#define RTC_PDN1		0x5b4
#define  RTC_GPIO_USER_MASK	0x1f00
#define RTC_WRTGR		0x5c2
#define RTC_CON			0x5c4
#define  RTC_CON_F32KOB		BIT(5)

/* MT6358 top, clock, DCXO and power-sequence registers */
#define TOP_CON			0x01e
#define  SRCLKEN_IN0_HW_MODE	BIT(1)
#define  SRCLKEN_IN1_HW_MODE	BIT(3)
#define TOP_TMA_KEY		0x3a8
#define  TOP_TMA_KEY_UNLOCK	0x9ca7
#define SCK_TOP_CKPDN_CON0	0x51a
#define SCK_TOP_CKPDN_CON0_CLR	0x51e
#define  RTC_EOSC32_CK_PDN	BIT(2)
#define EOSC_CALI_CON0		0x540
#define  EOSC_CALI_TD_MASK	GENMASK(7, 5)
#define  EOSC_CALI_TD_DEFAULT	(6 << 5)	/* vendor rtc_eosc_cali_td = 8 */
#define DCXO_CW02		0x790
#define  XO_EN32K_MAN		BIT(0)
#define PPCCTL0			0xa08
#define  RG_PWRHOLD		BIT(0)
#define PSEQ_ELR11		0xa62
#define  RG_SDN_DLY_ENB		BIT(10)

/* SoC TOPRGU (watchdog), vendor wdt_v2/mtk_wdt.h */
#define WDT_MODE		0x00
#define  WDT_MODE_EN		BIT(0)
#define  WDT_MODE_EXTEN		BIT(2)
#define  WDT_MODE_IRQ		BIT(3)
#define  WDT_MODE_AUTO_RESTART	BIT(4)	/* vendor "bypass power key" flag */
#define  WDT_MODE_IRQ_LEVEL_EN	BIT(5)
#define  WDT_MODE_DUAL_MODE	BIT(6)
#define  WDT_MODE_DDR_RESERVE	BIT(7)
#define  WDT_MODE_KEY		0x22000000
#define WDT_RESTART		0x08
#define  WDT_RESTART_KEY	0x1971
#define WDT_SWRST		0x14
#define  WDT_SWRST_KEY		0x1209

#define RTC_POLL_US		10
#define RTC_TIMEOUT_US		500000	/* vendor rtc_busy_wait(): 500 ms */

struct mt6358_pwrc {
	struct device *dev;
	struct regmap *map;
	void __iomem *toprgu;
	int err;		/* first regmap error; no writes after it */
	bool prepared;
};

static u32 pwrc_read(struct mt6358_pwrc *p, u32 reg)
{
	u32 val = 0;
	int ret;

	if (p->err)
		return 0;
	ret = regmap_read(p->map, reg, &val);
	if (ret)
		p->err = ret;
	return val;
}

static void pwrc_write(struct mt6358_pwrc *p, u32 reg, u32 val)
{
	int ret;

	if (p->err)
		return;
	ret = regmap_write(p->map, reg, val);
	if (ret)
		p->err = ret;
}

/* vendor pmic_config_interface(): read, replace the field, always write */
static void pwrc_field(struct mt6358_pwrc *p, u32 reg, u32 mask, u32 val)
{
	u32 cur = pwrc_read(p, reg);

	pwrc_write(p, reg, (cur & ~mask) | (val & mask));
}

/* vendor rtc_busy_wait() */
static void pwrc_rtc_busy_wait(struct mt6358_pwrc *p)
{
	u32 val;

	if (p->err)
		return;
	if (regmap_read_poll_timeout(p->map, RTC_BBPU, val,
				     !(val & RTC_BBPU_CBUSY),
				     RTC_POLL_US, RTC_TIMEOUT_US))
		dev_warn(p->dev, "RTC busy timeout, BBPU 0x%x\n", val);
}

/* vendor rtc_write_trigger() */
static void pwrc_rtc_trigger(struct mt6358_pwrc *p)
{
	pwrc_write(p, RTC_WRTGR, 1);
	pwrc_rtc_busy_wait(p);
}

static void pwrc_bbpu_reload(struct mt6358_pwrc *p)
{
	pwrc_write(p, RTC_BBPU,
		   pwrc_read(p, RTC_BBPU) | RTC_BBPU_KEY | RTC_BBPU_RELOAD);
	pwrc_rtc_trigger(p);
}

/* vendor rtc_disable_2sec_reboot() */
static void pwrc_disable_2sec_reboot(struct mt6358_pwrc *p)
{
	pwrc_write(p, RTC_AL_SEC, pwrc_read(p, RTC_AL_SEC) &
		   ~(RTC_BBPU_2SEC_EN | RTC_BBPU_AUTO_PDN_SEL));
	pwrc_rtc_trigger(p);
}

/* vendor rtc_enable_k_eosc() */
static void pwrc_enable_k_eosc(struct mt6358_pwrc *p)
{
	u32 val;

	pwrc_field(p, SCK_TOP_CKPDN_CON0_CLR, RTC_EOSC32_CK_PDN,
		   RTC_EOSC32_CK_PDN);
	pwrc_field(p, TOP_CON, SRCLKEN_IN0_HW_MODE, SRCLKEN_IN0_HW_MODE);
	pwrc_field(p, TOP_CON, SRCLKEN_IN1_HW_MODE, SRCLKEN_IN1_HW_MODE);
	pwrc_field(p, SCK_TOP_CKPDN_CON0, RTC_EOSC32_CK_PDN, 0);
	pwrc_field(p, EOSC_CALI_CON0, EOSC_CALI_TD_MASK, EOSC_CALI_TD_DEFAULT);
	/* DCXO from 32k-less mode to RTC mode, else EOSC calibration fails */
	pwrc_field(p, DCXO_CW02, XO_EN32K_MAN, 0);
	pwrc_bbpu_reload(p);

	/* K EOSC mode for a normal power-off followed by battery removal */
	val = pwrc_read(p, RTC_AL_YEA);
	pwrc_write(p, RTC_AL_YEA, ((val | RTC_K_EOSC_RSV_0) &
		   ~RTC_K_EOSC_RSV_1) | RTC_K_EOSC_RSV_2);
	pwrc_rtc_trigger(p);

	/* rtc_xosc_write(osc32 | RTC_EMBCK_SRC_SEL, true) */
	val = pwrc_read(p, RTC_OSC32CON) | RTC_EMBCK_SRC_SEL;
	pwrc_write(p, RTC_OSC32CON, RTC_OSC32CON_UNLOCK1);
	pwrc_rtc_busy_wait(p);
	pwrc_write(p, RTC_OSC32CON, RTC_OSC32CON_UNLOCK2);
	pwrc_rtc_busy_wait(p);
	pwrc_write(p, RTC_OSC32CON, val);
	pwrc_rtc_busy_wait(p);
	pwrc_bbpu_reload(p);
}

/* hal_rtc_bbpu_pwdn() up to, not including, rtc_bbpu_pwrdown() */
static int mt6358_pwrc_prepare(struct sys_off_data *data)
{
	struct mt6358_pwrc *p = data->cb_data;
	u32 val;

	pwrc_disable_2sec_reboot(p);
	pwrc_enable_k_eosc(p);

	/* stop the 32 kHz export if there are no RTC_GPIO users */
	if (!(pwrc_read(p, RTC_PDN1) & RTC_GPIO_USER_MASK) && !p->err) {
		pwrc_write(p, RTC_CON, pwrc_read(p, RTC_CON) | RTC_CON_F32KOB);
		pwrc_rtc_trigger(p);
	}

	/* lpsd */
	pwrc_write(p, RTC_BBPU, RTC_BBPU_KEY | RTC_BBPU_CLR | RTC_BBPU_PWREN);
	pwrc_write(p, RTC_AL_MASK, RTC_AL_MASK_DOW);
	pwrc_rtc_trigger(p);

	/* rtc_spar_alarm_clear_wait() */
	if (!p->err &&
	    regmap_read_poll_timeout(p->map, RTC_BBPU, val,
				     !(val & RTC_BBPU_CLR),
				     RTC_POLL_US, RTC_TIMEOUT_US))
		dev_warn(p->dev, "spare/alarm clear timeout, BBPU 0x%x\n", val);

	/* wk_pmic_enable_sdn_delay() */
	pwrc_write(p, TOP_TMA_KEY, TOP_TMA_KEY_UNLOCK);
	pwrc_field(p, PSEQ_ELR11, RG_SDN_DLY_ENB, 0);
	pwrc_write(p, TOP_TMA_KEY, 0);

	pwrc_bbpu_reload(p);

	if (p->err)
		dev_emerg(p->dev, "RTC power-down sequence failed: %d\n", p->err);
	else
		p->prepared = true;

	return NOTIFY_DONE;
}

/*
 * vendor wdt_arch_reset() without WD_SW_RESET_BYPASS_PWR_KEY, as used by
 * arch_reset(0, "charger"); includes mtk_rgu_dram_reserved(0).
 */
static void mt6358_pwrc_wdt_reset(struct mt6358_pwrc *p)
{
	u32 mode;

	writel(WDT_RESTART_KEY, p->toprgu + WDT_RESTART);

	mode = readl(p->toprgu + WDT_MODE);
	mode &= ~(WDT_MODE_AUTO_RESTART | WDT_MODE_IRQ | WDT_MODE_IRQ_LEVEL_EN |
		  WDT_MODE_EN | WDT_MODE_DUAL_MODE);
	mode |= WDT_MODE_KEY | WDT_MODE_EXTEN;
	writel(mode, p->toprgu + WDT_MODE);

	mode = readl(p->toprgu + WDT_MODE) & ~WDT_MODE_DDR_RESERVE;
	writel(mode | WDT_MODE_KEY, p->toprgu + WDT_MODE);

	udelay(100);
	writel(WDT_SWRST_KEY, p->toprgu + WDT_SWRST);
	mdelay(1000);
}

static int mt6358_pwrc_power_off(struct sys_off_data *data)
{
	struct mt6358_pwrc *p = data->cb_data;
	int i;

	if (!p->prepared)
		dev_emerg(p->dev, "power-down sequence incomplete\n");

	/* rtc_bbpu_pwrdown() -> PMIC_POWER_HOLD(0) */
	p->err = 0;
	pwrc_field(p, PPCCTL0, RG_PWRHOLD, 0);

	/* vendor mt_power_off(): wait up to 11 x 100 ms for the cut */
	for (i = 0; i < 11; i++)
		mdelay(100);

	if (!p->toprgu) {
		dev_emerg(p->dev, "still powered, no TOPRGU for the fallback\n");
		return NOTIFY_DONE;
	}

	dev_emerg(p->dev, "still powered: reset without bypass power key\n");
	mt6358_pwrc_wdt_reset(p);

	/* reset did not happen: let the kernel's own path continue */
	return NOTIFY_DONE;
}

static int mt6358_pwrc_probe(struct platform_device *pdev)
{
	struct mt6397_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct device_node *np;
	struct mt6358_pwrc *p;
	int ret;

	if (!chip || chip->chip_id != MT6358_CHIP_ID)
		return dev_err_probe(dev, -ENODEV, "not an MT6358\n");

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->dev = dev;
	p->map = chip->regmap;

	/* TOPRGU is owned by mtk_wdt: map it without requesting the region */
	np = of_parse_phandle(dev->of_node, "mediatek,toprgu", 0);
	if (np) {
		p->toprgu = of_iomap(np, 0);
		of_node_put(np);
	}
	if (!p->toprgu)
		dev_warn(dev, "no TOPRGU: no fallback reset\n");

	ret = devm_register_sys_off_handler(dev, SYS_OFF_MODE_POWER_OFF_PREPARE,
					    SYS_OFF_PRIO_HIGH,
					    mt6358_pwrc_prepare, p);
	if (ret)
		return dev_err_probe(dev, ret, "power-off prepare handler\n");

	ret = devm_register_sys_off_handler(dev, SYS_OFF_MODE_POWER_OFF,
					    SYS_OFF_PRIO_HIGH,
					    mt6358_pwrc_power_off, p);
	if (ret)
		return dev_err_probe(dev, ret, "power-off handler\n");

	dev_info(dev, "power-off ready (PPCCTL0 0x%x, TOPRGU %s)\n",
		 pwrc_read(p, PPCCTL0), p->toprgu ? "mapped" : "none");
	return 0;
}

static const struct of_device_id mt6358_pwrc_dt_match[] = {
	{ .compatible = "mediatek,mt6358-pwrc" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6358_pwrc_dt_match);

static struct platform_driver mt6358_pwrc_driver = {
	.probe = mt6358_pwrc_probe,
	.driver = {
		.name = "mt6358-pwrc",
		.of_match_table = mt6358_pwrc_dt_match,
	},
};
module_platform_driver(mt6358_pwrc_driver);

MODULE_DESCRIPTION("MediaTek MT6358 PMIC power-off");
MODULE_LICENSE("GPL");
