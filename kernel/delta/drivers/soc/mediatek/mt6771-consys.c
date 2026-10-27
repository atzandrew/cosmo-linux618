// SPDX-License-Identifier: GPL-2.0
/*
 * MT6771 CONSYS power-on check (Cosmo Communicator, Wi-Fi step W1a).
 *
 * Runs the stock 4.4 CONSYS power-on order once at probe
 * (connectivity/common/common_main/platform/mtk_wcn_consys_hw.c
 * mtk_wcn_consys_hw_reg_ctrl() with the mt6771.c ops, co_clock_type 0;
 * docs/wifi-stock.md), reads the CONSYS chip ID, then powers CONSYS off
 * again in the stock power-off order. Nothing stays powered after probe.
 *
 * Mainline APIs replace the vendor ones:
 *   VCN18 / VCN28 / VCN33     -> regulators (stock voltages)
 *   A-die pinmux (GPIO113-121) -> pinctrl states "on" / "off"
 *   TOPRGU CONN MCU reset      -> reset line from mtk_wdt (BIT(12), key 0x88)
 *   SCP_SYS_CONN               -> the stock MTCMOS sequence, see below
 * CONSYS registers are touched only while the CONN domain is on.
 *
 * CONN power domain: mainline's scpsys driver does not probe on this
 * board yet (the DISP domain wants mmsys clocks, mmsys is disabled), so
 * there is no genpd provider. Until that is fixed this module runs the
 * stock CONN MTCMOS sequence itself (clk-mt6771-pg.c
 * spm_mtcmos_ctrl_conn(), the code behind stock SCP_SYS_CONN): SPM
 * CONN_PWR_CON 0x1000632c, PWR_STATUS/_2ND 0x10006180/0x184 bit 1, INFRA
 * AXI bus protect bits 13+14 (SET 0x100012a0, CLR 0x100012a4, STA1
 * 0x10001228). These are the registers mainline mt8183-pm-domains.h uses
 * for CONN. The "conn-power" child node is not used yet.
 *
 * Not done here (W1a is power only): EMI remap, EMI coredump reset, the
 * VCN28 HW0 op-mode bit (PMIC 0x1d8a bit 1, sleep behaviour), firmware.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/sizes.h>

/* CONN_MCU_CONFIG (reg 0), offsets from stock mt6771.h */
#define CONSYS_CHIP_ID		0x008
#define CONSYS_MCU_CFG_ACR	0x140
#define CONSYS_CPUPCR		0x160
#define CONSYS_ROM_DESEL	0x600
#define CONSYS_HANG_DBG_1	0x400
#define CONSYS_HANG_DBG_2	0x404
#define CONSYS_HANG_DBG_3	0x40c

#define CONSYS_CHIP_ID_MT6771	0x0788
#define CONSYS_ACR_MBIST_BYPASS	(BIT(0) | BIT(1))
#define CONSYS_ROM_DESEL_VAL	((0x17 << 16) | 0x2a4)

/* AFE (reg 1), stock CONSYS_AFE_REG_SETTING values */
#define AFE_RG_WBG_PLL_03	0x38
#define AFE_RG_WBG_PLL_03_VAL	0x000c1df0
#define AFE_RG_WBG_GPS_02	0x54
#define AFE_RG_WBG_GPS_02_VAL	0x110a2000

/* SPM (stock clk-mt6771-pg.c) */
#define SPM_BASE_PA		0x10006000UL
#define SPM_POWERON_CONFIG_EN	0x000
#define SPM_PWR_STATUS		0x180
#define SPM_PWR_STATUS_2ND	0x184
#define SPM_CONN_PWR_CON	0x32c
#define PWR_RST_B		BIT(0)
#define PWR_ISO			BIT(1)
#define PWR_ON			BIT(2)
#define PWR_ON_2ND		BIT(3)
#define PWR_CLK_DIS		BIT(4)
#define CONN_TOP2_ACK		BIT(30)	/* stock mt6771.c fallback polls it */
#define CONN_PWR_STA_MASK	BIT(1)

/* INFRACFG_AO bus protect (stock clk-mt6771-pg.c) */
#define INFRA_BASE_PA		0x10001000UL
#define INFRA_PROT_STA1		0x228
#define INFRA_PROT_SET		0x2a0
#define INFRA_PROT_CLR		0x2a4
#define CONN_PROT_MASK		(BIT(13) | BIT(14))

#define POLL_TRIES		1000	/* x 10 us; stock polls forever */

#define VCN33_UV		3500000

struct consys {
	struct device *dev;
	void __iomem *mcu;
	void __iomem *spm;
	void __iomem *infra;
	void __iomem *afe;
	struct regulator *vcn18, *vcn28, *vcn33;
	struct reset_control *mcu_rst;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_on, *pins_off;
};

static void consys_log_spm(struct consys *c, const char *when)
{
	dev_info(c->dev, "%s: CONN_PWR_CON 0x%08x PWR_STATUS 0x%08x/0x%08x PROT_STA1 0x%08x\n",
		 when, readl(c->spm + SPM_CONN_PWR_CON),
		 readl(c->spm + SPM_PWR_STATUS), readl(c->spm + SPM_PWR_STATUS_2ND),
		 readl(c->infra + INFRA_PROT_STA1));
}

static bool conn_is_on(struct consys *c)
{
	return (readl(c->spm + SPM_PWR_STATUS) & CONN_PWR_STA_MASK) ||
	       (readl(c->spm + SPM_PWR_STATUS_2ND) & CONN_PWR_STA_MASK);
}

static void conn_rmw(struct consys *c, u32 clr, u32 set)
{
	u32 v = readl(c->spm + SPM_CONN_PWR_CON);

	writel((v & ~clr) | set, c->spm + SPM_CONN_PWR_CON);
}

/* stock spm_mtcmos_ctrl_conn(STA_POWER_ON) */
static int conn_mtcmos_on(struct consys *c)
{
	int i;

	conn_rmw(c, 0, PWR_ON);
	conn_rmw(c, 0, PWR_ON_2ND);
	for (i = 0; i < POLL_TRIES; i++) {
		if ((readl(c->spm + SPM_PWR_STATUS) & CONN_PWR_STA_MASK) &&
		    (readl(c->spm + SPM_PWR_STATUS_2ND) & CONN_PWR_STA_MASK))
			break;
		udelay(10);
	}
	if (i == POLL_TRIES) {
		dev_err(c->dev, "CONN power ack timeout\n");
		return -ETIMEDOUT;
	}
	conn_rmw(c, PWR_CLK_DIS, 0);
	conn_rmw(c, PWR_ISO, 0);
	conn_rmw(c, 0, PWR_RST_B);
	writel(CONN_PROT_MASK, c->infra + INFRA_PROT_CLR);
	return 0;
}

/* stock spm_mtcmos_ctrl_conn(STA_POWER_DOWN) */
static int conn_mtcmos_off(struct consys *c)
{
	int i;

	writel(CONN_PROT_MASK, c->infra + INFRA_PROT_SET);
	for (i = 0; i < POLL_TRIES; i++) {
		if ((readl(c->infra + INFRA_PROT_STA1) & CONN_PROT_MASK) == CONN_PROT_MASK)
			break;
		udelay(10);
	}
	if (i == POLL_TRIES) {
		/* Cutting power with the bus not protected can hang the bus. */
		dev_err(c->dev, "CONN bus protect ack timeout, leaving CONN on\n");
		return -ETIMEDOUT;
	}
	conn_rmw(c, 0, PWR_ISO);
	conn_rmw(c, 0, PWR_CLK_DIS);
	conn_rmw(c, PWR_RST_B, 0);
	conn_rmw(c, PWR_ON, 0);
	conn_rmw(c, PWR_ON_2ND, 0);
	for (i = 0; i < POLL_TRIES; i++) {
		if (!conn_is_on(c))
			return 0;
		udelay(10);
	}
	dev_err(c->dev, "CONN power-off status timeout\n");
	return -ETIMEDOUT;
}

/* Stock power-off order, from whatever point power-on reached. */
static void consys_power_off(struct consys *c, int reached)
{
	dev_info(c->dev, "power off (reached step %d)\n", reached);
	if (reached >= 7) {
		dev_info(c->dev, "off: CONN MTCMOS\n");
		if (conn_mtcmos_off(c)) {
			consys_log_spm(c, "CONN off failed");
			dev_err(c->dev, "stopping power-off here: supplies stay on\n");
			return;
		}
	}
	if (reached >= 5) {
		dev_info(c->dev, "off: VCN28\n");
		regulator_disable(c->vcn28);
	}
	if (reached >= 3) {
		dev_info(c->dev, "off: A-die pinmux to GPIO\n");
		pinctrl_select_state(c->pinctrl, c->pins_off);
	}
	if (reached >= 2) {
		dev_info(c->dev, "off: VCN33, VCN18\n");
		regulator_disable(c->vcn33);
		regulator_disable(c->vcn18);
	}
	consys_log_spm(c, "after power off");
}

static int consys_power_on_check(struct consys *c)
{
	u32 id = 0, v;
	int i, ret, step;

	step = 2;
	dev_info(c->dev, "step 2: VCN18 on (1.8 V), VCN33 %d uV on\n", VCN33_UV);
	ret = regulator_enable(c->vcn18);
	if (ret)
		goto fail_early;
	ret = regulator_set_voltage(c->vcn33, VCN33_UV, VCN33_UV);
	if (!ret)
		ret = regulator_enable(c->vcn33);
	if (ret) {
		regulator_disable(c->vcn18);
		goto fail_early;
	}

	step = 3;
	dev_info(c->dev, "step 3: A-die pinmux GPIO113-121 to CONN\n");
	ret = pinctrl_select_state(c->pinctrl, c->pins_on);
	if (ret)
		goto fail;
	udelay(150);

	step = 4;
	dev_info(c->dev, "step 5: VCN28 on (2.8 V)\n");
	ret = regulator_enable(c->vcn28);
	if (ret)
		goto fail;
	step = 5;

	dev_info(c->dev, "step 6: assert CONN MCU SW reset\n");
	ret = reset_control_assert(c->mcu_rst);
	if (ret)
		goto fail;

	consys_log_spm(c, "before CONN on");
	dev_info(c->dev, "step 7: CONN power domain on\n");
	ret = conn_mtcmos_on(c);
	step = 7;	/* run the off sequence even after a failed on */
	consys_log_spm(c, "after CONN on");
	if (ret)
		goto fail;

	/* stock mt6771.c fallback: poll conn_top2 ack (CONN_PWR_CON bit 30) */
	for (i = 0; i < 100; i++) {
		if (readl(c->spm + SPM_CONN_PWR_CON) & CONN_TOP2_ACK)
			break;
		udelay(10);
	}
	/* stock clk-mt6771-pg.c (the path stock runs) does not check it */
	if (i == 100)
		dev_warn(c->dev, "CONN top2 ack (bit 30) not set after 1 ms\n");
	udelay(10);

	dev_info(c->dev, "step 8: poll chip ID\n");
	for (i = 0; i < 10; i++) {
		id = readl(c->mcu + CONSYS_CHIP_ID);
		if (id == CONSYS_CHIP_ID_MT6771)
			break;
		msleep(20);
	}
	dev_info(c->dev, "chip ID 0x%08x after %d tries (expect 0x%04x)\n",
		 id, i + 1, CONSYS_CHIP_ID_MT6771);
	if (id != CONSYS_CHIP_ID_MT6771) {
		dev_err(c->dev, "hang dbg 0x%08x 0x%08x 0x%08x\n",
			readl(c->mcu + CONSYS_HANG_DBG_1),
			readl(c->mcu + CONSYS_HANG_DBG_2),
			readl(c->mcu + CONSYS_HANG_DBG_3));
		ret = -ENODEV;
		goto fail;
	}

	dev_info(c->dev, "step 9: ROM desel 0x%08x\n", CONSYS_ROM_DESEL_VAL);
	writel(CONSYS_ROM_DESEL_VAL, c->mcu + CONSYS_ROM_DESEL);
	dev_info(c->dev, "ROM desel reads 0x%08x\n", readl(c->mcu + CONSYS_ROM_DESEL));

	dev_info(c->dev, "step 10: MCU ACR |= 0x3 (bypass at-speed MBIST)\n");
	v = readl(c->mcu + CONSYS_MCU_CFG_ACR);
	writel(v | CONSYS_ACR_MBIST_BYPASS, c->mcu + CONSYS_MCU_CFG_ACR);

	dev_info(c->dev, "step 11: AFE PLL_03 / GPS_02\n");
	writel(AFE_RG_WBG_PLL_03_VAL, c->afe + AFE_RG_WBG_PLL_03);
	writel(AFE_RG_WBG_GPS_02_VAL, c->afe + AFE_RG_WBG_GPS_02);

	dev_info(c->dev, "step 12: release CONN MCU SW reset\n");
	ret = reset_control_deassert(c->mcu_rst);
	if (ret)
		goto fail;
	msleep(20);

	for (i = 0; i < 3; i++) {
		dev_info(c->dev, "MCU running: CPUPCR 0x%08x\n",
			 readl(c->mcu + CONSYS_CPUPCR));
		msleep(5);
	}

	dev_info(c->dev, "PASS: CONSYS powered, chip ID 0x%04x\n", id);
	consys_power_off(c, step);
	return 0;

fail:
	consys_power_off(c, step);
	dev_err(c->dev, "FAIL at step %d (%d)\n", step, ret);
	return ret;
fail_early:
	dev_err(c->dev, "FAIL at step %d (%d)\n", step, ret);
	return ret;
}

static int consys_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *mem_np;
	struct reserved_mem *rmem;
	struct consys *c;
	int ret;

	c = devm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	c->dev = dev;

	c->mcu = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(c->mcu))
		return PTR_ERR(c->mcu);
	c->afe = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(c->afe))
		return PTR_ERR(c->afe);

	c->vcn18 = devm_regulator_get(dev, "vcn18");
	if (IS_ERR(c->vcn18))
		return dev_err_probe(dev, PTR_ERR(c->vcn18), "vcn18\n");
	c->vcn28 = devm_regulator_get(dev, "vcn28");
	if (IS_ERR(c->vcn28))
		return dev_err_probe(dev, PTR_ERR(c->vcn28), "vcn28\n");
	c->vcn33 = devm_regulator_get(dev, "vcn33");
	if (IS_ERR(c->vcn33))
		return dev_err_probe(dev, PTR_ERR(c->vcn33), "vcn33\n");

	c->mcu_rst = devm_reset_control_get_exclusive(dev, "mcu");
	if (IS_ERR(c->mcu_rst))
		return dev_err_probe(dev, PTR_ERR(c->mcu_rst), "mcu reset\n");

	c->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(c->pinctrl))
		return dev_err_probe(dev, PTR_ERR(c->pinctrl), "pinctrl\n");
	c->pins_on = pinctrl_lookup_state(c->pinctrl, "on");
	c->pins_off = pinctrl_lookup_state(c->pinctrl, "off");
	if (IS_ERR(c->pins_on) || IS_ERR(c->pins_off))
		return dev_err_probe(dev, -EINVAL, "pinctrl states on/off\n");

	mem_np = of_parse_phandle(dev->of_node, "memory-region", 0);
	rmem = mem_np ? of_reserved_mem_lookup(mem_np) : NULL;
	of_node_put(mem_np);
	if (!rmem)
		return dev_err_probe(dev, -EINVAL, "consys reserved memory\n");
	dev_info(dev, "reserved memory %pa size 0x%llx (2 MiB aligned: %s)\n",
		 &rmem->base, (unsigned long long)rmem->size,
		 IS_ALIGNED(rmem->base, SZ_2M) ? "yes" : "NO");

	/*
	 * SPM and INFRACFG_AO belong to other nodes (syscons); map without
	 * claiming the regions, as the vendor driver does.
	 */
	c->spm = devm_ioremap(dev, SPM_BASE_PA, SZ_4K);
	c->infra = devm_ioremap(dev, INFRA_BASE_PA, SZ_4K);
	if (!c->spm || !c->infra)
		return -ENOMEM;

	dev_info(dev, "SPM POWERON_CONFIG_EN 0x%08x\n",
		 readl(c->spm + SPM_POWERON_CONFIG_EN));
	consys_log_spm(c, "at probe");
	if (conn_is_on(c)) {
		dev_info(dev, "CONN is on at probe; MCU reset, then stock CONN off first\n");
		ret = reset_control_assert(c->mcu_rst);
		if (!ret)
			ret = conn_mtcmos_off(c);
		consys_log_spm(c, "after CONN off");
		if (ret)
			return dev_err_probe(dev, ret, "CONN off failed, not running the sequence\n");
	}

	ret = consys_power_on_check(c);
	/* Stay bound either way so the log and state can be inspected. */
	if (ret)
		dev_err(dev, "power-on check failed (%d)\n", ret);
	return 0;
}

static const struct of_device_id consys_of_match[] = {
	{ .compatible = "mediatek,mt6771-consys" },
	{ }
};
MODULE_DEVICE_TABLE(of, consys_of_match);

static struct platform_driver consys_driver = {
	.probe = consys_probe,
	.driver = {
		.name = "mt6771-consys",
		.of_match_table = consys_of_match,
	},
};
module_platform_driver(consys_driver);

MODULE_DESCRIPTION("MT6771 CONSYS power-on check");
MODULE_LICENSE("GPL");
