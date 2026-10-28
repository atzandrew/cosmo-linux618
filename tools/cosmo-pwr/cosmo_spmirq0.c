// SPDX-License-Identifier: GPL-2.0
/* Claim the SPM IRQ0 the stock way: stock DT sleep@10006000 has
 * interrupts = <GIC_SPI 187 IRQ_TYPE_LEVEL_LOW> behind the MediaTek
 * sysirq polarity block. Mapping it through sysirq as LEVEL_LOW sets the
 * inverter bit; until then the idle (high) line reads as pending in the
 * GIC. Handler as stock mtk_spm.c spm_irq0_handler: read SPM_IRQ_STA and
 * ack it with SIP SPM_IRQ0_HANDLER (0xC200021C). */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/arm-smccc.h>

static void __iomem *spm, *gicd;
static int virq;
static uint count, last_isr;
module_param(count, uint, 0444);
module_param(last_isr, uint, 0444);

static void show(const char *tag)
{
	pr_info("cosmo_spmirq0: %s: GIC pending %d; SPM_IRQ_STA 0x%08x\n", tag,
		!!(readl(gicd + 0x200 + 24) & BIT(27)), readl(spm + 0x158));
}

static irqreturn_t spm_irq0(int irq, void *data)
{
	struct arm_smccc_res res;

	last_isr = readl(spm + 0x158);
	arm_smccc_smc(0xC200021C, last_isr, 0, 0, 0, 0, 0, 0, &res);
	count++;
	return IRQ_HANDLED;
}

static int __init cosmo_spmirq0_init(void)
{
	struct of_phandle_args a = { .args_count = 3, .args = { 0, 187, IRQ_TYPE_LEVEL_LOW } };
	int ret;

	spm = ioremap(0x10006000, 0x1000);
	gicd = ioremap(0x0c000000, 0x1000);
	a.np = of_find_compatible_node(NULL, NULL, "mediatek,mt6577-sysirq");
	if (!spm || !gicd || !a.np)
		return -ENODEV;
	show("before");
	virq = irq_create_of_mapping(&a);
	of_node_put(a.np);
	if (!virq)
		return -EINVAL;
	ret = request_irq(virq, spm_irq0, 0, "spm-irq0", NULL);
	if (ret)
		return ret;
	show("after map + request");
	return 0;
}

static void __exit cosmo_spmirq0_exit(void)
{
	free_irq(virq, NULL);
	irq_dispose_mapping(virq);
}
module_init(cosmo_spmirq0_init);
module_exit(cosmo_spmirq0_exit);
MODULE_LICENSE("GPL");
