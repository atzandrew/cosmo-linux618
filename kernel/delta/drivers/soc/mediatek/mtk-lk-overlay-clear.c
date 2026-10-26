// SPDX-License-Identifier: GPL-2.0-only
/*
 * Clear a MediaTek LK overlay buffer at boot.
 *
 * LK leaves OVL layers enabled when it jumps to the kernel. On the Cosmo
 * Communicator (MT6771) layer 3 sits above the framebuffer used by
 * simpledrm and holds the "orange state" unlocked-bootloader warning.
 * Filling that buffer with zero (alpha 0) makes the layer transparent;
 * the display hardware itself is left alone.
 */
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

static int lk_overlay_clear_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *buf;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	/* The buffer lies in LK's no-map framebuffer reservation: no request. */
	buf = ioremap_wc(res->start, resource_size(res));
	if (!buf)
		return -ENOMEM;

	memset_io(buf, 0, resource_size(res));
	wmb();
	iounmap(buf);

	dev_info(&pdev->dev, "cleared %pR\n", res);
	return 0;
}

static const struct of_device_id lk_overlay_clear_of_match[] = {
	{ .compatible = "mediatek,lk-overlay-clear" },
	{ }
};

static struct platform_driver lk_overlay_clear_driver = {
	.probe = lk_overlay_clear_probe,
	.driver = {
		.name = "mtk-lk-overlay-clear",
		.of_match_table = lk_overlay_clear_of_match,
	},
};
builtin_platform_driver(lk_overlay_clear_driver);
