// SPDX-License-Identifier: GPL-2.0
/* Read-only: USB2 PHY digital monitor (t-phy@11f40000, u2 port, COM bank
 * at +0x300; stock mt6771 mtk-phy-asic.h). Samples U2PHYDMON1 to show the
 * D+/D- line state (bits 23:22: 0 SE0, 1 J, 2 K, 3 SE1), host disconnect
 * (21), VBUS valid (26), IDDIG (27). Also dumps DTM0/DTM1 (force bits).
 * Loads, prints, and fails on purpose so it never stays loaded. */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>

#define U2_COM	0x11f40300

static int samples = 20;
module_param(samples, int, 0444);

static int __init cosmo_u2ls_init(void)
{
	void __iomem *c = ioremap(U2_COM, 0x100);
	int i, hist[4] = { 0 };
	u32 v = 0;

	if (!c)
		return -ENOMEM;
	pr_info("cosmo_u2ls: ACR0 %08x ACR6 %08x DCR0 %08x DCR1 %08x\n",
		readl(c + 0x00), readl(c + 0x18), readl(c + 0x60), readl(c + 0x64));
	pr_info("cosmo_u2ls: DTM0 %08x DTM1 %08x\n", readl(c + 0x68), readl(c + 0x6c));
	for (i = 0; i < samples; i++) {
		v = readl(c + 0x74);
		hist[(v >> 22) & 3]++;
		if (i < 3 || i == samples - 1)
			pr_info("cosmo_u2ls: DMON1 %08x line %u discon %u vbusvalid %u iddig %u\n",
				v, (v >> 22) & 3, (v >> 21) & 1, (v >> 26) & 1, (v >> 27) & 1);
		msleep(50);
	}
	pr_info("cosmo_u2ls: line state counts SE0 %d J %d K %d SE1 %d\n",
		hist[0], hist[1], hist[2], hist[3]);
	iounmap(c);
	return -EAGAIN;
}
module_init(cosmo_u2ls_init);
MODULE_LICENSE("GPL");
