// SPDX-License-Identifier: GPL-2.0-only
/* AW9524 current sinks; stock Cosmo aw9524_key.c, docs/leds-stock.md. */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/slab.h>

#define AW9524_ID       0x10
#define AW9524_CONTROL  0x11
#define AW9524_MODE_P0  0x12
#define AW9524_MODE_P1  0x13
#define AW9524_RESET    0x7f
#define AW9524_CHANNELS 16

struct aw9524_led {
	struct led_classdev cdev;
	struct i2c_client *client;
	u8 dim;
};

/* Pin indices P0_0..7 = 0..7, P1_0..7 = 8..15. */
static u8 aw9524_dim(unsigned int pin)
{
	if (pin < 8)
		return 0x24 + pin;
	if (pin < 12)
		return 0x20 + pin - 8;
	return 0x2c + pin - 12;
}

static int aw9524_brightness_set(struct led_classdev *cdev,
			       enum led_brightness value)
{
	struct aw9524_led *led = container_of(cdev, struct aw9524_led, cdev);

	return i2c_smbus_write_byte_data(led->client, led->dim, value);
}

static void aw9524_hold_reset(void *data)
{
	gpiod_set_value_cansleep(data, 1);
}

static int aw9524_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct gpio_desc *reset;
	unsigned long used = 0;
	int ret, pin;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EOPNOTSUPP;

	reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset))
		return dev_err_probe(dev, PTR_ERR(reset), "reset GPIO unavailable\n");
	i2c_set_clientdata(client, reset);
	ret = devm_add_action_or_reset(dev, aw9524_hold_reset, reset);
	if (ret)
		return ret;

	/* Active-low HWEN, stock low 5 ms then high 5 ms. */
	fsleep(5000);
	gpiod_set_value_cansleep(reset, 0);
	fsleep(5000);
	ret = i2c_smbus_read_byte_data(client, AW9524_ID);
	if (ret < 0)
		return dev_err_probe(dev, ret, "chip ID read failed\n");
	if (ret != 0x23)
		return dev_err_probe(dev, -ENODEV, "unexpected chip ID %#x\n", ret);

	ret = i2c_smbus_write_byte_data(client, AW9524_RESET, 0);
	if (ret)
		return ret;
	/* Start dark, including unused pins; stock's initial blue is omitted. */
	for (pin = 0; pin < AW9524_CHANNELS; pin++) {
		ret = i2c_smbus_write_byte_data(client, aw9524_dim(pin), 0);
		if (ret)
			return ret;
	}
	ret = i2c_smbus_write_byte_data(client, AW9524_CONTROL, 0x02);
	if (ret)
		return ret;
	ret = i2c_smbus_write_byte_data(client, 0x04, 0);
	if (ret)
		return ret;
	ret = i2c_smbus_write_byte_data(client, 0x05, 0);
	if (ret)
		return ret;
	ret = i2c_smbus_write_byte_data(client, AW9524_MODE_P0, 0);
	if (ret)
		return ret;
	ret = i2c_smbus_write_byte_data(client, AW9524_MODE_P1, 0);
	if (ret)
		return ret;

	device_for_each_child_node_scoped(dev, child) {
		struct led_init_data init = { .fwnode = child };
		struct aw9524_led *led;
		u32 index, limit;

		ret = fwnode_property_read_u32(child, "reg", &index);
		if (ret || index >= AW9524_CHANNELS || (used & BIT(index)))
			return -EINVAL;
		used |= BIT(index);
		ret = fwnode_property_read_u32(child, "max-brightness", &limit);
		if (ret || !limit || limit > 255)
			return -EINVAL;
		led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
		if (!led)
			return -ENOMEM;
		led->client = client;
		led->dim = aw9524_dim(index);
		led->cdev.max_brightness = limit;
		led->cdev.flags = LED_CORE_SUSPENDRESUME;
		led->cdev.brightness_set_blocking = aw9524_brightness_set;
		ret = devm_led_classdev_register_ext(dev, &led->cdev, &init);
		if (ret)
			return dev_err_probe(dev, ret, "LED registration failed\n");
	}
	if (!used)
		return -EINVAL;
	dev_info(dev, "AW9524 ID 0x23, LED current controls ready\n");
	return 0;
}

static void aw9524_shutdown(struct i2c_client *client)
{
	aw9524_hold_reset(i2c_get_clientdata(client));
}

static const struct of_device_id aw9524_of_match[] = {
	{ .compatible = "awinic,aw9524-leds" },
	{ }
};
MODULE_DEVICE_TABLE(of, aw9524_of_match);

static struct i2c_driver aw9524_driver = {
	.driver = {
		.name = "aw9524-leds",
		.of_match_table = aw9524_of_match,
	},
	.probe = aw9524_probe,
	.shutdown = aw9524_shutdown,
};
module_i2c_driver(aw9524_driver);
MODULE_DESCRIPTION("Awinic AW9524 LED current controller");
MODULE_LICENSE("GPL");
