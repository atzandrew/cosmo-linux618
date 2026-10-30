// SPDX-License-Identifier: GPL-2.0
/*
 * Test module (docs/cpufreq.md step C2): move the CCI from LK's
 * ccipll 1196 MHz / MCUCFG divider /2 to ccipll 598 MHz / divider /1
 * without the real rate ever going above 598 MHz, the order stock uses
 * (cpufreq_v1 mtk_cpufreq_main.c: raise a divider before the PLL
 * change, lower it after). Then, with exercise=1, run the CCI through
 * 546, 481 and 598 MHz with clk26m as the intermediate parent, as
 * mtk-cci-devfreq would with the planned DTS. Every step is a rate at or
 * below 598 MHz. Refuses unless the hardware is in LK's state.
 * Never stays loaded.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>

#define MCUCFG_BASE	0x0c530000
#define BUS_DIV		0x7c0		/* mux [10:9], clkdiv [21:17] */
#define DIV_SEL(v)	(((v) >> 17) & 0x1f)
#define APMIXED_BASE	0x1000c000
#define CCIPLL_CON1	0x294

static bool exercise;
module_param(exercise, bool, 0444);

static void __iomem *mc, *ap;

static unsigned long real_khz(void)
{
	u32 con1 = readl(ap + CCIPLL_CON1), div = readl(mc + BUS_DIV);
	u64 vco = ((u64)(con1 & 0x3fffff) * 26000) >> 14;
	u32 pd = 1 << ((con1 >> 24) & 7);
	u32 cd = DIV_SEL(div) == 8 ? 1 : DIV_SEL(div) == 10 ? 2 :
		 DIV_SEL(div) == 11 ? 4 : 0;
	u32 mux = (div >> 9) & 3;

	if (!cd)
		return 0;
	if (mux == 0)
		return 26000 / cd;
	if (mux != 1)
		return 1;	/* not ccipll: report as invalid */
	return div_u64(vco, pd * cd);
}

static void report(const char *what, struct clk *cci, struct clk *pll)
{
	pr_info("cosmo_ccidiv: %s: real %lu kHz, clk cci %lu ccipll %lu, DIV 0x%08x\n",
		what, real_khz(), clk_get_rate(cci) / 1000,
		clk_get_rate(pll) / 1000, readl(mc + BUS_DIV));
}

static int step(struct clk *cci, struct clk *pll, struct clk *inter,
		unsigned long hz)
{
	int ret = clk_set_parent(cci, inter);

	if (!ret)
		ret = clk_set_rate(pll, hz);
	if (!ret)
		ret = clk_set_parent(cci, pll);
	report(ret ? "step FAILED" : "step", cci, pll);
	return ret;
}

static int __init cosmo_ccidiv_init(void)
{
	struct device_node *np = of_find_node_by_path("/cci");
	struct clk *cci = NULL, *inter = NULL, *pll;
	u32 v;
	int ret = -EINVAL;

	if (!np)
		return -ENODEV;
	cci = of_clk_get_by_name(np, "cci");
	of_node_put(np);
	np = of_find_node_by_path("/oscillator");
	if (np) {
		struct of_phandle_args args = { .np = np, .args_count = 0 };

		inter = of_clk_get_from_provider(&args);
		of_node_put(np);
	}
	mc = ioremap(MCUCFG_BASE, 0x1000);
	ap = ioremap(APMIXED_BASE, 0x1000);
	if (IS_ERR_OR_NULL(cci) || IS_ERR_OR_NULL(inter) || !mc || !ap) {
		pr_err("cosmo_ccidiv: missing clock or mapping: cci %ld inter %ld\n",
		       PTR_ERR_OR_ZERO(cci), PTR_ERR_OR_ZERO(inter));
		goto out;
	}
	pll = clk_get_parent(cci);
	report("before", cci, pll);

	v = readl(mc + BUS_DIV);
	if (DIV_SEL(v) == 10 && clk_get_rate(pll) == 1196000000UL &&
	    ((v >> 9) & 3) == 1) {
		/* 1: PLL 1196 -> 598 MHz, real 598 -> 299 MHz */
		ret = clk_set_rate(pll, 598000000UL);
		report(ret ? "pll FAILED" : "pll 598", cci, pll);
		if (ret)
			goto out;
		udelay(20);
		/* 2: divider /2 -> /1, real 299 -> 598 MHz */
		writel((readl(mc + BUS_DIV) & ~(0x1f << 17)) | (8 << 17),
		       mc + BUS_DIV);
		udelay(1);
		report("div /1", cci, pll);
	} else if (DIV_SEL(v) == 8) {
		pr_info("cosmo_ccidiv: divider already /1\n");
	} else {
		report("REFUSED: not LK's state", cci, pll);
		goto out;
	}
	ret = 0;
	if (exercise && !(ret = step(cci, pll, inter, 546000000UL)) &&
	    !(ret = step(cci, pll, inter, 481000000UL)))
		ret = step(cci, pll, inter, 598000000UL);
out:
	if (!IS_ERR_OR_NULL(cci))
		clk_put(cci);
	if (!IS_ERR_OR_NULL(inter))
		clk_put(inter);
	if (mc)
		iounmap(mc);
	if (ap)
		iounmap(ap);
	return ret ? ret : -EAGAIN;
}
module_init(cosmo_ccidiv_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cosmo: CCI divider /2 -> /1 at the same rate (test)");
