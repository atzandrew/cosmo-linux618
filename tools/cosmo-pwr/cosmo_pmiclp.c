// SPDX-License-Identifier: GPL-2.0
/*
 * Read-only: MT6358 regulator sleep-control registers (OP_EN at the
 * address below, OP_CFG at +6) next to what stock PMIC_LP_INIT_SETTING
 * (pmic/mt6358/v1/pmic_initial_setting.c) programs. OP_EN bits:
 * 0 SW, 1 SRCLKEN0 (suspend), 2 SRCLKEN1, 3 SRCLKEN2 (deep idle).
 * OP_CFG bit n (HW users): 1 = low power, 0 = off when that signal drops.
 * Expected values are the bits stock sets; stock never clears other bits.
 * Never stays loaded (-EAGAIN).
 */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_platform.h>

struct lp { const char *name; u16 adr; u8 en, cfg; };

/* en/cfg = bits stock sets (suspend + deep idle sections) */
static const struct lp tbl[] = {
	{ "vproc11", 0x1390, 0x1, 0 }, { "vcore", 0x1490, 0x1, 0 },
	{ "vgpu", 0x1510, 0x1, 0 }, { "vmodem", 0x1590, 0x1, 0 },
	{ "vs1", 0x1690, 0x1, 0 }, { "vs2", 0x1710, 0xa, 0xa },
	{ "vdram1", 0x1610, 0xa, 0xa }, { "vproc12", 0x1410, 0x1, 0 },
	{ "vsram_gpu", 0x1bd0, 0x1, 0 }, { "vsram_others", 0x1bae, 0xa, 0xa },
	{ "vsram_proc11", 0x1b4e, 0x1, 0 }, { "vxo22", 0x1a8a, 0xa, 0xa },
	{ "vrf18", 0x1c1e, 0x4, 0 }, { "vrf12", 0x1c32, 0x4, 0 },
	{ "vefuse", 0x1c46, 0x1, 0 }, { "vcn33", 0x1d1e, 0x1, 0 },
	{ "vcn28", 0x1d8a, 0x1, 0 }, { "vcn18", 0x1c5a, 0x1, 0 },
	{ "vcama1", 0x1c6e, 0x1, 0 }, { "vcamd", 0x1c9e, 0x1, 0 },
	{ "vcama2", 0x1c8a, 0x1, 0 }, { "vsram_proc12", 0x1b90, 0x1, 0 },
	{ "vcamio", 0x1cb2, 0x1, 0 }, { "vldo28", 0x1d34, 0x1, 0 },
	{ "va12", 0x1a9e, 0xa, 0xa }, { "vaux18", 0x1ab2, 0xa, 0xa },
	{ "vaud28", 0x1ac6, 0x2, 0x2 }, { "vio28", 0x1ada, 0x1, 0 },
	{ "vio18", 0x1aee, 0x1, 0 }, { "vfe28", 0x1c0a, 0x4, 0 },
	{ "vdram2", 0x1b0a, 0xa, 0xa }, { "vmc", 0x1cc6, 0x1, 0 },
	{ "vmch", 0x1cda, 0x1, 0 }, { "vemc", 0x1b1e, 0x1, 0 },
	{ "vsim1", 0x1d4a, 0x1, 0 }, { "vsim2", 0x1d5e, 0x1, 0 },
	{ "vibr", 0x1d0a, 0x1, 0 }, { "vusb", 0x1b32, 0xa, 0xa },
	{ "vbif28", 0x1da0, 0xa, 0 },
};

static int __init cosmo_pmiclp_init(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, "mediatek,mt8183-pwrap");
	struct platform_device *pdev;
	struct regmap *map;
	unsigned int en, cfg;
	int i, miss = 0;

	if (!np)
		np = of_find_node_by_name(NULL, "pwrap");
	pdev = np ? of_find_device_by_node(np) : NULL;
	of_node_put(np);
	map = pdev ? dev_get_regmap(&pdev->dev, NULL) : NULL;
	if (!map) {
		pr_err("cosmo_pmiclp: no pwrap regmap\n");
		goto out;
	}
	for (i = 0; i < ARRAY_SIZE(tbl); i++) {
		const struct lp *l = &tbl[i];
		bool ok;

		if (regmap_read(map, l->adr, &en) || regmap_read(map, l->adr + 6, &cfg)) {
			pr_info("cosmo_pmiclp: %-13s read failed\n", l->name);
			continue;
		}
		ok = (en & l->en) == l->en && (cfg & l->en & ~1u) == l->cfg;
		miss += !ok;
		pr_info("cosmo_pmiclp: %-13s 0x%04x OP_EN 0x%04x OP_CFG 0x%04x  stock en %x cfg %x  %s\n",
			l->name, l->adr, en, cfg, l->en, l->cfg, ok ? "ok" : "DIFF");
	}
	pr_info("cosmo_pmiclp: %d rails differ from stock\n", miss);
out:
	if (pdev)
		put_device(&pdev->dev);
	return -EAGAIN;
}
module_init(cosmo_pmiclp_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cosmo: read MT6358 regulator sleep-control vs stock");
