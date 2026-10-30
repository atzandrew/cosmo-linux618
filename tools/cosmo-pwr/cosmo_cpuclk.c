// SPDX-License-Identifier: GPL-2.0
/* Read-only: CPU/CCI clock state as stock mt6771 cpufreq decodes it
 * (cpufreq_v1 mach/mt6771/mtk_cpufreq_platform.c): ARMPLL/CCIPLL CON1
 * (DDS [21:0], posdiv [26:24]) and the MCUCFG PLL dividers
 * (mux [10:9], clkdiv [21:17]: 8 = /1, 10 = /2, 11 = /4). */
#include <linux/module.h>
#include <linux/io.h>

#define APMIXED_BASE 0x1000c000
#define MCUCFG_BASE 0x0c530000

static const struct {
	const char *name;
	u32 con1, div;
} plls[] = {
	{ "LL (armpll_ll)", 0x204, 0x7a0 },
	{ "L (armpll_l)", 0x214, 0x7a8 },
	{ "CCI (ccipll)", 0x294, 0x7c0 },
};

static int __init cosmo_cpuclk_init(void)
{
	void __iomem *ap = ioremap(APMIXED_BASE, 0x1000);
	void __iomem *mc = ioremap(MCUCFG_BASE, 0x1000);
	int i;

	if (!ap || !mc) {
		if (ap)
			iounmap(ap);
		if (mc)
			iounmap(mc);
		return -ENOMEM;
	}
	for (i = 0; i < ARRAY_SIZE(plls); i++) {
		u32 con0 = readl(ap + plls[i].con1 - 4);
		u32 con1 = readl(ap + plls[i].con1);
		u32 div = readl(mc + plls[i].div);
		u32 pd = (con1 >> 24) & 7, cd = (div >> 17) & 0x1f;
		u64 vco = ((u64)(con1 & 0x3fffff) * 26000) >> 14; /* kHz */
		u32 pdv = pd < 4 ? 1 << pd : 0;
		u32 cdv = cd == 8 ? 1 : cd == 10 ? 2 : cd == 11 ? 4 : 0;

		pr_info("cosmo_cpuclk: %s CON0 0x%08x CON1 0x%08x DIV 0x%08x: vco %llu kHz posdiv /%u mux %u clkdiv sel %u (/%u) -> %llu kHz\n",
			plls[i].name, con0, con1, div, vco, pdv, (div >> 9) & 3,
			cd, cdv, pdv && cdv ? div_u64(vco, pdv * cdv) : 0);
	}
	iounmap(ap);
	iounmap(mc);
	return -EAGAIN; /* report only, never stays loaded */
}
module_init(cosmo_cpuclk_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cosmo: read CPU/CCI PLL and divider state (read-only)");
