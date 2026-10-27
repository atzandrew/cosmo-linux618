// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cosmo keyboard backlight: stock MT6771 PWM1 FIFO and shared GPIO178.
 * Vendor receipts and the HDMI ownership restriction: docs/leds-stock.md.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define COSMO_PWM_ENABLE	0x00
#define COSMO_PWM_CON		0x10
#define COSMO_PWM_HDUR		0x14
#define COSMO_PWM_LDUR		0x18
#define COSMO_PWM_GDUR		0x1c
#define COSMO_PWM_DATA0		0x30
#define COSMO_PWM_DATA1		0x34
#define COSMO_PWM_WAVENUM	0x38
#define COSMO_PWM_CLKDIV		GENMASK(2, 0)
#define COSMO_PWM_STOP_BITS	GENMASK(14, 9)

/* CLK_DIV8 = 3; block clock, FIFO, periodic, idle/guard low, new mode. */
#define COSMO_PWM_FIFO_CON	(FIELD_PREP(COSMO_PWM_STOP_BITS, 63) | \
				 FIELD_PREP(COSMO_PWM_CLKDIV, 3))

struct cosmo_kbd {
	struct device *dev;
	void __iomem *base;
	struct clk_bulk_data clocks[3];
	struct gpio_desc *enable;
	struct gpio_desc *hdmi_detect;
	struct led_classdev led;
	/* Serializes brightness, clock lifetime and shared-enable decisions. */
	struct mutex lock;
	bool running;
};

static const u32 cosmo_kbd_data[][2] = {
	{ 0, 0 },
	{ 0x00001fff, 0 },
	{ 0x07ffffff, 0 },
	{ 0xffffffff, 0x0000003f },
	{ 0xffffffff, 0x000fffff },
	{ 0xffffffff, 0xffffffff },
};

static int cosmo_kbd_set(struct led_classdev *led,
			 enum led_brightness brightness)
{
	struct cosmo_kbd *kbd = container_of(led, struct cosmo_kbd, led);
	bool new_clocks = false;
	int detect, ret = 0;
	u32 enabled;

	if (brightness >= ARRAY_SIZE(cosmo_kbd_data))
		return -EINVAL;

	mutex_lock(&kbd->lock);
	if (!brightness) {
		/* A failed detect read must never cause shared power to drop. */
		detect = gpiod_get_value_cansleep(kbd->hdmi_detect);
		if (kbd->running) {
			enabled = readl(kbd->base + COSMO_PWM_ENABLE);
			writel(enabled & ~BIT(0), kbd->base + COSMO_PWM_ENABLE);
			readl(kbd->base + COSMO_PWM_ENABLE);
			clk_bulk_disable_unprepare(ARRAY_SIZE(kbd->clocks),
						   kbd->clocks);
			kbd->running = false;
		}
		if (detect < 0) {
			ret = detect;
			goto out;
		}
		/* Stock only lowers GPIO178 if GPIO54 is low. */
		if (!detect)
			ret = gpiod_direction_output(kbd->enable, 0);
		goto out;
	}

	if (!kbd->running) {
		ret = clk_bulk_prepare_enable(ARRAY_SIZE(kbd->clocks), kbd->clocks);
		if (ret)
			goto out;
		new_clocks = true;
	}

	/* Assert shared enable before starting PWM, as stock does. */
	ret = gpiod_direction_output(kbd->enable, 1);
	if (ret) {
		if (new_clocks)
			clk_bulk_disable_unprepare(ARRAY_SIZE(kbd->clocks),
						   kbd->clocks);
		goto out;
	}

	enabled = readl(kbd->base + COSMO_PWM_ENABLE);
	writel(enabled & ~BIT(0), kbd->base + COSMO_PWM_ENABLE);
	writel(COSMO_PWM_FIFO_CON, kbd->base + COSMO_PWM_CON);
	writel(1, kbd->base + COSMO_PWM_HDUR);
	writel(1, kbd->base + COSMO_PWM_LDUR);
	writel(0, kbd->base + COSMO_PWM_GDUR);
	writel(cosmo_kbd_data[brightness][0], kbd->base + COSMO_PWM_DATA0);
	writel(cosmo_kbd_data[brightness][1], kbd->base + COSMO_PWM_DATA1);
	writel(0, kbd->base + COSMO_PWM_WAVENUM);
	writel(enabled | BIT(0), kbd->base + COSMO_PWM_ENABLE);
	readl(kbd->base + COSMO_PWM_ENABLE);
	kbd->running = true;
out:
	mutex_unlock(&kbd->lock);
	return ret;
}

static void cosmo_kbd_stop(void *data)
{
	struct cosmo_kbd *kbd = data;
	int ret;

	ret = cosmo_kbd_set(&kbd->led, LED_OFF);
	if (ret)
		dev_warn(kbd->dev, "Shared enable retained: %d\n", ret);
}

static int cosmo_kbd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cosmo_kbd *kbd;
	u32 enabled;
	int ret;

	kbd = devm_kzalloc(dev, sizeof(*kbd), GFP_KERNEL);
	if (!kbd)
		return -ENOMEM;
	kbd->dev = dev;
	mutex_init(&kbd->lock);
	kbd->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(kbd->base))
		return PTR_ERR(kbd->base);

	kbd->clocks[0].id = "top";
	kbd->clocks[1].id = "main";
	kbd->clocks[2].id = "pwm1";
	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(kbd->clocks), kbd->clocks);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot get PWM clocks\n");

	/* Never take over a PWM channel that firmware left running. */
	ret = clk_bulk_prepare_enable(ARRAY_SIZE(kbd->clocks), kbd->clocks);
	if (ret)
		return ret;
	enabled = readl(kbd->base + COSMO_PWM_ENABLE);
	clk_bulk_disable_unprepare(ARRAY_SIZE(kbd->clocks), kbd->clocks);
	if (enabled & GENMASK(3, 0))
		return dev_err_probe(dev, -EBUSY, "Firmware PWM still enabled\n");

	kbd->hdmi_detect = devm_gpiod_get(dev, "hdmi-detect", GPIOD_IN);
	if (IS_ERR(kbd->hdmi_detect))
		return dev_err_probe(dev, PTR_ERR(kbd->hdmi_detect),
				     "Cannot get HDMI detect\n");
	/* Do not change GPIO178's level on probe. */
	kbd->enable = devm_gpiod_get(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(kbd->enable))
		return dev_err_probe(dev, PTR_ERR(kbd->enable),
				     "Cannot get shared enable\n");

	kbd->led.name = "cosmo::kbd_backlight";
	kbd->led.max_brightness = ARRAY_SIZE(cosmo_kbd_data) - 1;
	kbd->led.brightness_set_blocking = cosmo_kbd_set;
	kbd->led.flags = LED_CORE_SUSPENDRESUME;
	platform_set_drvdata(pdev, kbd);

	/* Register cleanup first: unregister LED before releasing clocks/GPIOs. */
	ret = devm_add_action_or_reset(dev, cosmo_kbd_stop, kbd);
	if (ret)
		return ret;
	ret = devm_led_classdev_register(dev, &kbd->led);
	if (ret)
		return ret;
	dev_info(dev, "Stock PWM1 FIFO keyboard levels 0-5 ready\n");
	return 0;
}

static void cosmo_kbd_shutdown(struct platform_device *pdev)
{
	cosmo_kbd_stop(platform_get_drvdata(pdev));
}

static const struct of_device_id cosmo_kbd_match[] = {
	{ .compatible = "planet,cosmo-kbd-backlight" },
	{ }
};
MODULE_DEVICE_TABLE(of, cosmo_kbd_match);

static struct platform_driver cosmo_kbd_driver = {
	.probe = cosmo_kbd_probe,
	.shutdown = cosmo_kbd_shutdown,
	.driver = {
		.name = "cosmo-kbd-backlight",
		.of_match_table = cosmo_kbd_match,
	},
};
module_platform_driver(cosmo_kbd_driver);

MODULE_DESCRIPTION("Cosmo keyboard backlight using stock PWM1 FIFO levels");
MODULE_LICENSE("GPL");
