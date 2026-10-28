// SPDX-License-Identifier: GPL-2.0
/* Read-only: SPM PCM watchdog latch registers (stock mtk_spm_reg_mt6771.h
 * PCM_WDT_LATCH_0..11, WDT_LATCH_SPARE*, *_FIX) and SPM debug registers,
 * read after a PCM watchdog reset to see where the SPM firmware stopped. */
#include <linux/module.h>
#include <linux/io.h>

static int __init cosmo_latch_init(void)
{
	void __iomem *s = ioremap(0x10006000, 0x1000);
	static const u16 off[] = {
		0x780, 0x784, 0x788, 0x78c,
		0x800, 0x804, 0x808, 0x80c, 0x810, 0x814, 0x818, 0x81c, 0x820, 0x824,
		0x828, 0x82c, 0x830, 0x834, 0x838, 0x83c,
		0x130, 0x134, 0x15c, 0x160, 0x164, 0x178, 0x600, 0x604, 0x1c, 0x30, 0x34,
	};
	int i;

	if (!s)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(off); i++)
		pr_info("cosmo_latch: 0x%03x = 0x%08x\n", off[i], readl(s + off[i]));
	iounmap(s);
	return -EAGAIN;
}
module_init(cosmo_latch_init);
MODULE_LICENSE("GPL");
