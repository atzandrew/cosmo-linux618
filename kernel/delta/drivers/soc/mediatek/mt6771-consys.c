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
 *   SCP_SYS_CONN               -> CONN power domain (genpd, runtime PM)
 * CONSYS registers are touched only while the CONN domain is on.
 *
 * The CONN domain sits on the "conn-power" child node, not on the consys
 * node: platform probe powers a node's single domain on before probe
 * runs, which would turn CONN on before the supplies and the MCU reset.
 * dev_pm_domain_attach_by_id() attaches without powering on.
 *
 * Not done here (W1a is power only): EMI remap, EMI coredump reset, the
 * VCN28 HW0 op-mode bit (PMIC 0x1d8a bit 1, sleep behaviour), firmware.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_domain.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
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

/* SPM, read only (owned by scpsys): CONN power control and status */
#define SPM_CONN_PWR_CON_PA	0x1000632cUL
#define SPM_PWR_STATUS_PA	0x10006180UL
#define SPM_PWR_STATUS_2ND_PA	0x10006184UL
#define SPM_CONN_TOP2_ACK	BIT(30)
#define SPM_PWR_STATUS_CONN	BIT(1)

#define VCN33_UV		3500000

struct consys {
	struct device *dev;
	struct platform_device *pd_pdev;
	struct device *pd_dev;
	void __iomem *mcu;
	void __iomem *afe;
	struct regulator *vcn18, *vcn28, *vcn33;
	struct reset_control *mcu_rst;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_on, *pins_off;
};

static u32 spm_read(unsigned long pa)
{
	void __iomem *p = ioremap(pa, 4);
	u32 v;

	if (!p)
		return 0xdeadbeef;
	v = readl(p);
	iounmap(p);
	return v;
}

static void consys_log_spm(struct consys *c, const char *when)
{
	dev_info(c->dev, "%s: SPM CONN_PWR_CON 0x%08x PWR_STATUS 0x%08x PWR_STATUS_2ND 0x%08x\n",
		 when, spm_read(SPM_CONN_PWR_CON_PA), spm_read(SPM_PWR_STATUS_PA),
		 spm_read(SPM_PWR_STATUS_2ND_PA));
}

/* Stock power-off order, from whatever point power-on reached. */
static void consys_power_off(struct consys *c, int reached)
{
	dev_info(c->dev, "power off (reached step %d)\n", reached);
	if (reached >= 7) {
		dev_info(c->dev, "off: CONN domain\n");
		pm_runtime_put_sync(c->pd_dev);
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
	ret = pm_runtime_resume_and_get(c->pd_dev);
	if (ret)
		goto fail;
	step = 7;
	consys_log_spm(c, "after CONN on");

	/* stock: poll conn_top2 ack (CONN_PWR_CON bit 30) */
	for (i = 0; i < 100; i++) {
		if (spm_read(SPM_CONN_PWR_CON_PA) & SPM_CONN_TOP2_ACK)
			break;
		udelay(10);
	}
	if (i == 100) {
		dev_err(c->dev, "CONN top2 ack (bit 30) not set, not touching CONSYS\n");
		ret = -ETIMEDOUT;
		goto fail;
	}
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

static void consys_pd_pdev_del(void *data)
{
	struct consys *c = data;

	of_platform_device_destroy(&c->pd_pdev->dev, NULL);
}

static void consys_pd_detach(void *data)
{
	struct consys *c = data;

	dev_pm_domain_detach(c->pd_dev, false);
}

static int consys_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *mem_np, *pd_np;
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

	pd_np = of_get_child_by_name(dev->of_node, "conn-power");
	if (!pd_np)
		return dev_err_probe(dev, -EINVAL, "conn-power node\n");
	c->pd_pdev = of_platform_device_create(pd_np, NULL, dev);
	of_node_put(pd_np);
	if (!c->pd_pdev)
		return dev_err_probe(dev, -ENOMEM, "conn-power device\n");
	ret = devm_add_action_or_reset(dev, consys_pd_pdev_del, c);
	if (ret)
		return ret;
	c->pd_dev = dev_pm_domain_attach_by_id(&c->pd_pdev->dev, 0);
	if (IS_ERR_OR_NULL(c->pd_dev))
		return dev_err_probe(dev, c->pd_dev ? PTR_ERR(c->pd_dev) : -ENODEV,
				     "CONN power domain\n");
	ret = devm_add_action_or_reset(dev, consys_pd_detach, c);
	if (ret)
		return ret;
	consys_log_spm(c, "after attach");
	/*
	 * mtk-pm-domains powers every domain without KEEP_DEFAULT_OFF on at
	 * registration, and pd_ignore_unused keeps it on: CONN is on since
	 * boot, with the MCU out of reset and the supplies off. Stock powers
	 * CONN on only after the supplies and the MCU reset, so switch it off
	 * first (genpd powers off on the last runtime put).
	 */
	if (spm_read(SPM_PWR_STATUS_PA) & SPM_PWR_STATUS_CONN) {
		dev_info(dev, "CONN is on since boot; powering it off before the stock sequence\n");
		ret = pm_runtime_resume_and_get(c->pd_dev);
		if (ret)
			return dev_err_probe(dev, ret, "CONN get\n");
		pm_runtime_put_sync(c->pd_dev);
		consys_log_spm(c, "after CONN off");
		if (spm_read(SPM_PWR_STATUS_PA) & SPM_PWR_STATUS_CONN)
			return dev_err_probe(dev, -EBUSY, "CONN still on, not running the sequence\n");
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
