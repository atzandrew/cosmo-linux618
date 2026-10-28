// SPDX-License-Identifier: GPL-2.0
/*
 * MT6358 register tool for the sleep-current tests.
 *   rd=a,b,...           print these registers (read only)
 *   go=1 adr=a,... set=s,... clr=c,...
 *                        read-modify-write: new = (old | set) & ~clr,
 *                        as stock pmic_config_interface does; prints
 *                        old/new/readback. Nothing is written unless go=1.
 * Never stays loaded (-EAGAIN).
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define MAXR 32
static uint rd[MAXR], adr[MAXR], set[MAXR], clr[MAXR];
static int nrd, nadr, nset, nclr;
module_param_array(rd, uint, &nrd, 0444);
module_param_array(adr, uint, &nadr, 0444);
module_param_array(set, uint, &nset, 0444);
module_param_array(clr, uint, &nclr, 0444);
static bool go;
module_param(go, bool, 0444);

static int __init cosmo_pmicw_init(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, "mediatek,mt8183-pwrap");
	struct platform_device *pdev = np ? of_find_device_by_node(np) : NULL;
	struct regmap *map = pdev ? dev_get_regmap(&pdev->dev, NULL) : NULL;
	unsigned int v, n, rb;
	int i;

	of_node_put(np);
	if (!map) {
		pr_err("cosmo_pmicw: no pwrap regmap\n");
		goto out;
	}
	for (i = 0; i < nrd; i++) {
		if (regmap_read(map, rd[i], &v))
			pr_info("cosmo_pmicw: rd 0x%04x failed\n", rd[i]);
		else
			pr_info("cosmo_pmicw: rd 0x%04x = 0x%04x\n", rd[i], v);
	}
	if (nadr && (nset != nadr || nclr != nadr)) {
		pr_err("cosmo_pmicw: adr/set/clr counts differ; nothing written\n");
		goto out;
	}
	for (i = 0; i < nadr; i++) {
		if (regmap_read(map, adr[i], &v)) {
			pr_err("cosmo_pmicw: 0x%04x read failed; stop\n", adr[i]);
			break;
		}
		n = (v | set[i]) & ~clr[i];
		if (!go) {
			pr_info("cosmo_pmicw: DRY 0x%04x 0x%04x -> 0x%04x\n", adr[i], v, n);
			continue;
		}
		regmap_write(map, adr[i], n);
		regmap_read(map, adr[i], &rb);
		pr_info("cosmo_pmicw: WR 0x%04x 0x%04x -> 0x%04x (readback 0x%04x)\n",
			adr[i], v, n, rb);
	}
out:
	if (pdev)
		put_device(&pdev->dev);
	return -EAGAIN;
}
module_init(cosmo_pmicw_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cosmo: MT6358 register read / read-modify-write for tests");
