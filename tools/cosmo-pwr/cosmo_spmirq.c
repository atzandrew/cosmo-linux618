// SPDX-License-Identifier: GPL-2.0
/* SPM IRQ0 (SPI 187, INTID 219) state: GIC enable/pending and SPM_IRQ_STA.
 * clear=1 makes stock's ack, SIP SPM_IRQ0_HANDLER (0xC200021C) with the
 * ISR status (stock mtk_spm.c spm_irq0_handler), then reads again. */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/arm-smccc.h>

static bool clear;
module_param(clear, bool, 0444);

static void __iomem *g, *s;

static void show(const char *tag)
{
	pr_info("cosmo_spmirq: %s: GIC enabled %d pending %d active %d; SPM_IRQ_STA 0x%08x IRQ_MASK 0x%08x\n",
		tag, !!(readl(g + 0x100 + 24) & BIT(27)), !!(readl(g + 0x200 + 24) & BIT(27)),
		!!(readl(g + 0x300 + 24) & BIT(27)), readl(s + 0x158), readl(s + 0xb4));
}

static int __init cosmo_spmirq_init(void)
{
	struct arm_smccc_res res;

	g = ioremap(0x0c000000, 0x1000);
	s = ioremap(0x10006000, 0x1000);
	if (!g || !s)
		return -ENOMEM;
	show("now");
	if (clear) {
		arm_smccc_smc(0xC200021C, readl(s + 0x158), 0, 0, 0, 0, 0, 0, &res);
		show("after SIP ack");
	}
	iounmap(g);
	iounmap(s);
	return -EAGAIN;
}
module_init(cosmo_spmirq_init);
MODULE_LICENSE("GPL");
