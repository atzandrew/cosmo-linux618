// SPDX-License-Identifier: GPL-2.0
/* Read-only: SSPM mailbox 0/1 slot tags (written by the SSPM firmware at
 * boot; stock check_table_tag, mt6771 sspm_ipi_define.h) and the mailbox
 * IN/OUT IRQ registers (stock DT sspm@10440000, sspm_define.h). */
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

static int __init cosmo_sspm_peek_init(void)
{
	peek(0, 0x10450000, 0x10451000, 16);
	peek(1, 0x10460000, 0x10461000, 20);
	return -EAGAIN;
}
module_init(cosmo_sspm_peek_init);
MODULE_LICENSE("GPL");
