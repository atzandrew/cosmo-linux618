// SPDX-License-Identifier: GPL-2.0
/*
 * Test module: give the SSPM MCDI (CPU idle) controller stock's init
 * values and watch its state. Stock mcdi_governor_init() ->
 * mcdi_avail_cpu_cluster_update() writes the online CPU mask to
 * AVAIL_CPU_MASK (mailbox 3 slot 7); mcdi_init() writes 0 to
 * CPU_ISOLATION_MASK (slot 12). Nothing else: no CLUSTER_CAN_POWER_OFF,
 * no BUCK_POWER_OFF_MASK. write=0 only reads. Never stays loaded.
 */
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>

#define MCDI_MBOX	0x10480000
#define SLOT(n)		(4 * (n))

static bool write;
module_param(write, bool, 0444);

static void dump(void __iomem *b, const char *when)
{
	pr_info("cosmo_mcdi: %s: avail 0x%x iso 0x%x pwr_stat 0x%x action %u cnt %u/%u pending 0x%x slot5 0x%x slot16 0x%x\n",
		when, readl(b + SLOT(7)), readl(b + SLOT(12)), readl(b + SLOT(8)),
		readl(b + SLOT(9)), readl(b + SLOT(10)), readl(b + SLOT(11)),
		readl(b + SLOT(14)), readl(b + SLOT(5)), readl(b + SLOT(16)));
}

static int __init cosmo_mcdi_init(void)
{
	void __iomem *b = ioremap(MCDI_MBOX, 0x100);
	u32 mask = 0;
	int cpu;

	if (!b)
		return -ENOMEM;
	dump(b, "before");
	if (write) {
		for_each_online_cpu(cpu)
			mask |= BIT(cpu);
		writel(mask, b + SLOT(7));
		writel(0, b + SLOT(12));
		msleep(20);
		dump(b, "after 20 ms");
		msleep(1000);
		dump(b, "after 1 s");
	}
	iounmap(b);
	return -EAGAIN;
}
module_init(cosmo_mcdi_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cosmo: SSPM MCDI mailbox init values as stock (test)");
