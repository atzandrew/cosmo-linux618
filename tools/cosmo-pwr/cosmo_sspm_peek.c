// SPDX-License-Identifier: GPL-2.0
/* Read-only: SSPM mailbox 0/1 slot tags (written by the SSPM firmware at
 * boot; stock check_table_tag, mt6771 sspm_ipi_define.h) and the mailbox
 * IN/OUT IRQ registers (stock DT sspm@10440000, sspm_define.h).
 * Mailbox 3 = the SSPM MCDI (CPU idle) controller; slot names from stock
 * mcdi/include/mcdi_v1/mtk_mcdi_mbox.h. */
#include <linux/module.h>
#include <linux/io.h>

static void peek(int mb, phys_addr_t base, phys_addr_t ctrl, int n)
{
	void __iomem *b = ioremap(base, 0x100), *c = ioremap(ctrl, 8);
	char buf[200];
	int i, len = 0;

	if (!b || !c)
		goto out;
	for (i = 0; i < n; i++)
		len += scnprintf(buf + len, sizeof(buf) - len, " %08x", readl(b + 4 * i));
	pr_info("cosmo_sspm: mbox%d IN_IRQ 0x%x OUT_IRQ 0x%x slots:%s\n",
		mb, readl(c), readl(c + 4), buf);
out:
	if (b)
		iounmap(b);
	if (c)
		iounmap(c);
}

static const char * const mcdi_slot[16] = {
	"CL0_CAN_POWER_OFF", "CL1_CAN_POWER_OFF", "BUCK_POWER_OFF_MASK",
	"CL0_ATF_ACTION_DONE", "CL1_ATF_ACTION_DONE", "RESERVED",
	"PAUSE_ACTION", "AVAIL_CPU_MASK", "CPU_CLUSTER_PWR_STAT",
	"ACTION_STAT", "CL0_CNT", "CL1_CNT", "CPU_ISOLATION_MASK",
	"PAUSE_ACK", "PENDING_ON_EVENT", "PROF_CMD",
};

static void peek_mcdi(void)
{
	void __iomem *b = ioremap(0x10480000, 0x100);
	int i;

	if (!b)
		return;
	for (i = 0; i < 16; i++)
		pr_info("cosmo_sspm: mcdi[%2d] %-20s 0x%08x\n", i, mcdi_slot[i],
			readl(b + 4 * i));
	iounmap(b);
}

static int __init cosmo_sspm_peek_init(void)
{
	peek(0, 0x10450000, 0x10451000, 16);
	peek(1, 0x10460000, 0x10461000, 20);
	peek(3, 0x10480000, 0x10481000, 16);
	peek_mcdi();
	return -EAGAIN;
}
module_init(cosmo_sspm_peek_init);
MODULE_LICENSE("GPL");
