// SPDX-License-Identifier: GPL-2.0
/*
 * Test: drive the data-out bit of an already-output GPIO (MT6771 GPIO
 * block, DOUT 0x100 + 0x10 per 32 pins, SET +4 / CLR +8), without
 * claiming it. Refuses pins that are not outputs. Nothing happens unless
 * go=1. Used for GPIO160 (stock: sil9022_hdmi_pwren, HDMI power, also the
 * keyboard-light supply; stock drives it low with HDMI and light off).
 * Never stays loaded (-EAGAIN).
 */
#include <linux/io.h>
#include <linux/module.h>

#define GPIO_BASE	0x10005000
#define DIR(p)		(0x000 + ((p) / 32) * 0x10)
#define DOUT(p)		(0x100 + ((p) / 32) * 0x10)

static uint pin = 160;
module_param(pin, uint, 0444);
static uint val;
module_param(val, uint, 0444);
static bool go;
module_param(go, bool, 0444);

static int __init cosmo_gpo_init(void)
{
	void __iomem *b = ioremap(GPIO_BASE, 0x1000);
	u32 bit = BIT(pin % 32), dir, before;

	if (!b || pin >= 192)
		return -EINVAL;
	dir = readl(b + DIR(pin));
	before = readl(b + DOUT(pin));
	if (!(dir & bit)) {
		pr_err("cosmo_gpo: GPIO%u is not an output; refused\n", pin);
	} else if (go) {
		writel(bit, b + DOUT(pin) + (val ? 4 : 8));
		pr_info("cosmo_gpo: GPIO%u out %d -> %d (now %d)\n", pin,
			!!(before & bit), !!val, !!(readl(b + DOUT(pin)) & bit));
	} else {
		pr_info("cosmo_gpo: GPIO%u output, out %d (dry run)\n", pin, !!(before & bit));
	}
	iounmap(b);
	return -EAGAIN;
}
module_init(cosmo_gpo_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cosmo test: set an output GPIO's level");
