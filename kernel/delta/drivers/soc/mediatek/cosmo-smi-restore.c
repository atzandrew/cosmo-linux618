// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cosmo Communicator: keep LK's SMI settings across a DISP power cycle.
 *
 * The display scans out physical buffers: the M4U, SMI common and SMI
 * larb0 drivers stay disabled, and LK leaves larb0 ports 0-5 (OVL, 2L
 * OVL, RDMA, WDMA) with the MMU off. When the DISP domain is switched off
 * in system suspend these registers reset (larb0 NONSEC_CON reads 0x1,
 * MMU on, on every port), and the display then shows no picture.
 *
 * This device sits in the DISP domain. Its suspend_noirq callback runs
 * before genpd switches the domain off and saves the registers below;
 * resume_noirq runs after genpd has switched it on again and writes them
 * back, before the display resumes. The mainline mtk-smi driver restores
 * the same registers in its runtime resume, but it needs the M4U.
 */
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

static const u16 common_offs[] = {
	0x100, 0x220, 0x230, 0x234, 0x238, 0x23c, 0x300, 0x444,
};

static const u16 larb_offs[] = {
	0x024, 0x040, 0x200, 0x204, 0x208,
	0x380, 0x384, 0x388, 0x38c, 0x390, 0x394, 0x398, 0x39c, 0x3a0, 0x3a4,
};

struct smi_restore {
	void __iomem *common;
	void __iomem *larb;
	u32 common_val[ARRAY_SIZE(common_offs)];
	u32 larb_val[ARRAY_SIZE(larb_offs)];
	bool saved;
};

static unsigned int restored;
module_param(restored, uint, 0444);
MODULE_PARM_DESC(restored, "register restores since boot");

static int smi_restore_suspend_noirq(struct device *dev)
{
	struct smi_restore *s = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(common_offs); i++)
		s->common_val[i] = readl(s->common + common_offs[i]);
	for (i = 0; i < ARRAY_SIZE(larb_offs); i++)
		s->larb_val[i] = readl(s->larb + larb_offs[i]);
	s->saved = true;
	return 0;
}

static int smi_restore_resume_noirq(struct device *dev)
{
	struct smi_restore *s = dev_get_drvdata(dev);
	int i;

	if (!s->saved)
		return 0;
	for (i = 0; i < ARRAY_SIZE(common_offs); i++)
		writel(s->common_val[i], s->common + common_offs[i]);
	for (i = 0; i < ARRAY_SIZE(larb_offs); i++)
		writel(s->larb_val[i], s->larb + larb_offs[i]);
	restored++;
	return 0;
}

static const struct dev_pm_ops smi_restore_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(smi_restore_suspend_noirq,
				  smi_restore_resume_noirq)
};

static int smi_restore_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct smi_restore *s;
	struct resource *res;

	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	/* The ranges belong to the (disabled) SMI nodes: map, do not request. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	s->common = devm_ioremap(dev, res->start, resource_size(res));
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		return -EINVAL;
	s->larb = devm_ioremap(dev, res->start, resource_size(res));
	if (!s->common || !s->larb)
		return -ENOMEM;

	platform_set_drvdata(pdev, s);
	dev_info(dev, "larb0 port0 NONSEC_CON 0x%08x\n", readl(s->larb + 0x380));
	return 0;
}

static const struct of_device_id smi_restore_of_match[] = {
	{ .compatible = "planet,cosmo-smi-restore" },
	{ }
};

static struct platform_driver smi_restore_driver = {
	.probe = smi_restore_probe,
	.driver = {
		.name = "cosmo-smi-restore",
		.of_match_table = smi_restore_of_match,
		.pm = pm_sleep_ptr(&smi_restore_pm_ops),
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(smi_restore_driver);

MODULE_DESCRIPTION("Cosmo Communicator: keep LK's SMI settings across a DISP power cycle");
MODULE_LICENSE("GPL");
