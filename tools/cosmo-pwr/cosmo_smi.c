// SPDX-License-Identifier: GPL-2.0
/* Read-only dump: SMI common (0x14019000) and larb0 (0x14017000) registers
 * that mainline mtk-smi.c programs for MT8183. Needs DIS on. */
#include <linux/module.h>
#include <linux/io.h>

static void dump(const char *name, phys_addr_t pa, const u16 *offs, int n)
{
	void __iomem *b = ioremap(pa, 0x1000);
	int i;

	if (!b)
		return;
	for (i = 0; i < n; i++)
		pr_info("cosmo_smi: %s+0x%03x = 0x%08x\n", name, offs[i], readl(b + offs[i]));
	iounmap(b);
}

static const u16 common_offs[] = { 0x100, 0x200, 0x220, 0x230, 0x234, 0x238, 0x23c, 0x300, 0x444 };
static const u16 larb_offs[] = { 0x00c, 0x024, 0x040, 0x380, 0x384, 0x388, 0x38c, 0x390,
				 0x394, 0x398, 0x39c, 0x3a0, 0x3a4, 0x200, 0x204, 0x208 };

static int __init init_(void)
{
	void __iomem *spm = ioremap(0x10006000, 0x1000);
	u32 s = readl(spm + 0x180);

	iounmap(spm);
	if (!(s & BIT(3))) {
		pr_info("cosmo_smi: DIS off, not reading\n");
		return -EAGAIN;
	}
	dump("common", 0x14019000, common_offs, ARRAY_SIZE(common_offs));
	dump("larb0", 0x14017000, larb_offs, ARRAY_SIZE(larb_offs));
	return -EAGAIN;
}
module_init(init_);
MODULE_LICENSE("GPL");
