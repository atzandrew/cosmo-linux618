// SPDX-License-Identifier: GPL-2.0-only
/*
 * Planet Cosmo Communicator: right USB-C port (FUSB301A CC logic).
 *
 * One SSUSB controller serves both USB-C ports. GPIO79 routes USB 2 data
 * and the right port's VBUS switch to the right port (high) or leaves data
 * on the left port (low). GPIO178 enables the 5 V rail shared by the
 * keyboard backlight and the right port's VBUS.
 *
 * Stock (vendor 4.4, drivers/misc/mediatek/usb_c/fusb302/usb_typec.c):
 * - InitializeFUSB300(): FUSB301A mode 0x10 (DRP).
 * - fusb300_eint_work(): GPIO166 low = right attach; the Android side then
 *   writes /proc/AEON_USB_CONTROL "1" = GPIO79 high + host (force_to_otg).
 *   Detach: "0" = GPIO79 low + host off.
 * - test_right_usb(), every 5 s: FUSB301A type & 0x18 == 0x08 (a source,
 *   e.g. a charger, on the right) -> GPIO178 low.
 *
 * This driver does the same in the kernel:
 * - a sink on the right -> GPIO178 held on, GPIO79 high, role host;
 *   detach -> GPIO79 low, previous role back;
 * - a source on the right -> GPIO178 forced off;
 * - GPIO178 is offered as a regulator ("right-vbus") so the keyboard
 *   backlight shares it instead of owning the GPIO;
 * - the SSUSB VBUS supply ("left-vbus") passes through this driver, so the
 *   left port's OTG boost stays off while the right port is the host
 *   (stock's right-port host never turns the left boost on).
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/usb/role.h>
#include <linux/workqueue.h>

#define FUSB_DEVICE_ID		0x01
#define FUSB_MODE		0x02
#define  FUSB_MODE_DRP		0x10
#define FUSB_RESET		0x05
#define  FUSB_RESET_SW		BIT(0)
#define FUSB_STATUS		0x11
#define  FUSB_STATUS_ATTACH	BIT(0)
#define FUSB_TYPE		0x12
#define  FUSB_TYPE_SOURCE	BIT(3)
#define  FUSB_TYPE_SINK		BIT(4)
#define FUSB_INTERRUPT		0x13

/* Stock test_right_usb() runs every 5 s. */
#define FUSB_POLL_MS		5000
/* Stock fusb301a-pin debounce. */
#define FUSB_DEBOUNCE_MS	64
/* Host role up before the right port is powered (see right_attach). */
#define FUSB_HOST_SETTLE_MS	200

struct cosmo_fusb {
	struct device *dev;
	struct i2c_client *client;
	struct gpio_desc *mux;		/* GPIO79: high = right port */
	struct gpio_desc *vbus;		/* GPIO178: shared 5 V enable */
	struct regulator *left_vbus;	/* MT6370 OTG boost (left port) */
	struct usb_role_switch *role_sw;
	struct delayed_work poll;
	struct work_struct left_work;

	/* Protects everything below. */
	struct mutex lock;
	bool kbd_want;		/* right-vbus regulator enabled by a consumer */
	bool left_req;		/* left-vbus regulator enabled by SSUSB */
	bool left_on;		/* MT6370 OTG boost actually enabled */
	bool right_host;	/* sink on the right, host role ours */
	bool source_seen;	/* source on the right: GPIO178 must stay off */
	enum usb_role prev_role;
};

static int cosmo_fusb_read(struct cosmo_fusb *fusb, u8 reg)
{
	return i2c_smbus_read_byte_data(fusb->client, reg);
}

/* Lock held. */
static void cosmo_fusb_apply_vbus(struct cosmo_fusb *fusb)
{
	bool on = (fusb->kbd_want || fusb->right_host) && !fusb->source_seen;

	gpiod_set_value_cansleep(fusb->vbus, on);
}

/* Lock held. Never called from a regulator op (see left_work). */
static void cosmo_fusb_apply_left(struct cosmo_fusb *fusb)
{
	bool want = fusb->left_req && !fusb->right_host;
	int ret;

	if (want == fusb->left_on)
		return;
	if (want)
		ret = regulator_enable(fusb->left_vbus);
	else
		ret = regulator_disable(fusb->left_vbus);
	if (ret) {
		dev_err(fusb->dev, "left VBUS %s failed: %d\n",
			want ? "on" : "off", ret);
		return;
	}
	fusb->left_on = want;
}

static void cosmo_fusb_left_work(struct work_struct *work)
{
	struct cosmo_fusb *fusb = container_of(work, struct cosmo_fusb,
					       left_work);

	mutex_lock(&fusb->lock);
	cosmo_fusb_apply_left(fusb);
	mutex_unlock(&fusb->lock);
}

/* Lock held. The SSUSB role switch is looked up late: SSUSB needs our
 * left-vbus regulator to probe, so it cannot exist before us. */
static struct usb_role_switch *cosmo_fusb_role_sw(struct cosmo_fusb *fusb)
{
	struct usb_role_switch *sw;

	if (fusb->role_sw)
		return fusb->role_sw;
	sw = usb_role_switch_get(fusb->dev);
	if (IS_ERR_OR_NULL(sw)) {
		dev_warn(fusb->dev, "SSUSB role switch not ready: %ld\n",
			 PTR_ERR(sw));
		return NULL;
	}
	fusb->role_sw = sw;
	return sw;
}

/* Lock held. */
static void cosmo_fusb_right_attach(struct cosmo_fusb *fusb)
{
	struct usb_role_switch *sw = cosmo_fusb_role_sw(fusb);

	if (!sw)
		return;
	fusb->prev_role = usb_role_switch_get_role(sw);
	fusb->right_host = true;
	/*
	 * The device must get power only once the host port is up: a device
	 * powered before the host role (plugged in through boot, or GPIO79
	 * raised first) stays unseen (line state SE0). GPIO79 also switches
	 * the right VBUS, so it goes last. mtu3 changes role from a work item;
	 * the wait covers it.
	 */
	cosmo_fusb_apply_left(fusb);
	cosmo_fusb_apply_vbus(fusb);
	usb_role_switch_set_role(sw, USB_ROLE_HOST);
	msleep(FUSB_HOST_SETTLE_MS);
	gpiod_set_value_cansleep(fusb->mux, 1);
	dev_info(fusb->dev, "right port: device attached, host\n");
}

/* Lock held. */
static void cosmo_fusb_right_detach(struct cosmo_fusb *fusb)
{
	fusb->right_host = false;
	usb_role_switch_set_role(fusb->role_sw, fusb->prev_role);
	gpiod_set_value_cansleep(fusb->mux, 0);
	cosmo_fusb_apply_vbus(fusb);
	cosmo_fusb_apply_left(fusb);
	dev_info(fusb->dev, "right port: detached, role %s\n",
		 usb_role_string(fusb->prev_role));
}

static void cosmo_fusb_update(struct cosmo_fusb *fusb)
{
	int status, type;
	bool attached, sink, source;

	mutex_lock(&fusb->lock);
	/* Reading clears the interrupt flags; the result is not needed. */
	cosmo_fusb_read(fusb, FUSB_INTERRUPT);
	status = cosmo_fusb_read(fusb, FUSB_STATUS);
	type = cosmo_fusb_read(fusb, FUSB_TYPE);
	if (status < 0 || type < 0) {
		dev_warn_ratelimited(fusb->dev, "read failed: %d %d\n",
				     status, type);
		goto out;
	}

	attached = status & FUSB_STATUS_ATTACH;
	sink = attached && (type & FUSB_TYPE_SINK);
	source = attached && (type & FUSB_TYPE_SOURCE) && !sink;

	if (source != fusb->source_seen) {
		fusb->source_seen = source;
		dev_info(fusb->dev, "right port: source %s (status 0x%02x type 0x%02x)\n",
			 source ? "attached, 5 V off" : "gone", status, type);
		cosmo_fusb_apply_vbus(fusb);
	}

	if (sink && !fusb->right_host)
		cosmo_fusb_right_attach(fusb);
	else if (!sink && fusb->right_host)
		cosmo_fusb_right_detach(fusb);
out:
	mutex_unlock(&fusb->lock);
}

static irqreturn_t cosmo_fusb_irq(int irq, void *data)
{
	struct cosmo_fusb *fusb = data;

	msleep(FUSB_DEBOUNCE_MS);
	cosmo_fusb_update(fusb);
	return IRQ_HANDLED;
}

static void cosmo_fusb_poll(struct work_struct *work)
{
	struct cosmo_fusb *fusb = container_of(to_delayed_work(work),
					       struct cosmo_fusb, poll);

	cosmo_fusb_update(fusb);
	queue_delayed_work(system_freezable_wq, &fusb->poll,
			   msecs_to_jiffies(FUSB_POLL_MS));
}

/* Soft reset, then DRP as stock. A reset also makes the chip re-detect a
 * device that was plugged in before boot. */
static int cosmo_fusb_init_chip(struct cosmo_fusb *fusb)
{
	int id, ret;

	id = cosmo_fusb_read(fusb, FUSB_DEVICE_ID);
	if (id < 0)
		return id;
	ret = i2c_smbus_write_byte_data(fusb->client, FUSB_RESET,
					FUSB_RESET_SW);
	if (ret)
		return ret;
	usleep_range(10000, 11000);
	ret = i2c_smbus_write_byte_data(fusb->client, FUSB_MODE,
					FUSB_MODE_DRP);
	if (ret)
		return ret;
	dev_info(fusb->dev, "FUSB301A id 0x%02x, DRP\n", id);
	return 0;
}

/* right-vbus: GPIO178 for the keyboard backlight. */
static int cosmo_right_vbus_enable(struct regulator_dev *rdev)
{
	struct cosmo_fusb *fusb = rdev_get_drvdata(rdev);

	mutex_lock(&fusb->lock);
	fusb->kbd_want = true;
	cosmo_fusb_apply_vbus(fusb);
	mutex_unlock(&fusb->lock);
	return 0;
}

static int cosmo_right_vbus_disable(struct regulator_dev *rdev)
{
	struct cosmo_fusb *fusb = rdev_get_drvdata(rdev);

	mutex_lock(&fusb->lock);
	fusb->kbd_want = false;
	cosmo_fusb_apply_vbus(fusb);
	mutex_unlock(&fusb->lock);
	return 0;
}

static int cosmo_right_vbus_is_enabled(struct regulator_dev *rdev)
{
	struct cosmo_fusb *fusb = rdev_get_drvdata(rdev);
	int on;

	mutex_lock(&fusb->lock);
	on = fusb->kbd_want;
	mutex_unlock(&fusb->lock);
	return on;
}

static const struct regulator_ops cosmo_right_vbus_ops = {
	.enable = cosmo_right_vbus_enable,
	.disable = cosmo_right_vbus_disable,
	.is_enabled = cosmo_right_vbus_is_enabled,
};

/*
 * left-vbus: SSUSB's VBUS supply. The MT6370 boost is switched from a
 * work item: enabling another regulator from inside a regulator op would
 * nest regulator locks.
 */
static int cosmo_left_vbus_enable(struct regulator_dev *rdev)
{
	struct cosmo_fusb *fusb = rdev_get_drvdata(rdev);

	mutex_lock(&fusb->lock);
	fusb->left_req = true;
	mutex_unlock(&fusb->lock);
	schedule_work(&fusb->left_work);
	return 0;
}

static int cosmo_left_vbus_disable(struct regulator_dev *rdev)
{
	struct cosmo_fusb *fusb = rdev_get_drvdata(rdev);

	mutex_lock(&fusb->lock);
	fusb->left_req = false;
	mutex_unlock(&fusb->lock);
	schedule_work(&fusb->left_work);
	return 0;
}

static int cosmo_left_vbus_is_enabled(struct regulator_dev *rdev)
{
	struct cosmo_fusb *fusb = rdev_get_drvdata(rdev);
	int on;

	mutex_lock(&fusb->lock);
	on = fusb->left_req;
	mutex_unlock(&fusb->lock);
	return on;
}

static const struct regulator_ops cosmo_left_vbus_ops = {
	.enable = cosmo_left_vbus_enable,
	.disable = cosmo_left_vbus_disable,
	.is_enabled = cosmo_left_vbus_is_enabled,
};

static const struct regulator_desc cosmo_fusb_regs[] = {
	{
		.name = "right-vbus",
		.of_match = "right-vbus",
		.regulators_node = "regulators",
		.ops = &cosmo_right_vbus_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.n_voltages = 1,
		.fixed_uV = 5000000,
	},
	{
		.name = "left-vbus",
		.of_match = "left-vbus",
		.regulators_node = "regulators",
		.ops = &cosmo_left_vbus_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.n_voltages = 1,
		.fixed_uV = 5000000,
	},
};

static void cosmo_fusb_cleanup(void *data)
{
	struct cosmo_fusb *fusb = data;

	cancel_delayed_work_sync(&fusb->poll);
	cancel_work_sync(&fusb->left_work);
	mutex_lock(&fusb->lock);
	if (fusb->right_host)
		cosmo_fusb_right_detach(fusb);
	fusb->left_req = false;
	cosmo_fusb_apply_left(fusb);
	mutex_unlock(&fusb->lock);
	if (fusb->role_sw)
		usb_role_switch_put(fusb->role_sw);
}

static int cosmo_fusb_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regulator_config cfg = { };
	struct cosmo_fusb *fusb;
	struct regulator_dev *rdev;
	int i, ret;

	fusb = devm_kzalloc(dev, sizeof(*fusb), GFP_KERNEL);
	if (!fusb)
		return -ENOMEM;
	fusb->dev = dev;
	fusb->client = client;
	fusb->prev_role = USB_ROLE_DEVICE;
	mutex_init(&fusb->lock);
	INIT_DELAYED_WORK(&fusb->poll, cosmo_fusb_poll);
	INIT_WORK(&fusb->left_work, cosmo_fusb_left_work);
	i2c_set_clientdata(client, fusb);

	/* Data stays on the left until a device appears on the right. */
	fusb->mux = devm_gpiod_get(dev, "mux", GPIOD_OUT_LOW);
	if (IS_ERR(fusb->mux))
		return dev_err_probe(dev, PTR_ERR(fusb->mux), "no mux GPIO\n");
	/* Stock fusb300_gpio_init(): GPIO178 low until someone needs it. */
	fusb->vbus = devm_gpiod_get(dev, "vbus", GPIOD_OUT_LOW);
	if (IS_ERR(fusb->vbus))
		return dev_err_probe(dev, PTR_ERR(fusb->vbus), "no vbus GPIO\n");
	fusb->left_vbus = devm_regulator_get(dev, "left-vbus");
	if (IS_ERR(fusb->left_vbus))
		return dev_err_probe(dev, PTR_ERR(fusb->left_vbus),
				     "no left-vbus supply\n");

	ret = devm_add_action_or_reset(dev, cosmo_fusb_cleanup, fusb);
	if (ret)
		return ret;

	/*
	 * Regulators first and unconditionally: SSUSB (left port, USB
	 * networking) waits for left-vbus, so a chip problem must not keep
	 * this driver from binding.
	 */
	cfg.dev = dev;
	cfg.driver_data = fusb;
	for (i = 0; i < ARRAY_SIZE(cosmo_fusb_regs); i++) {
		rdev = devm_regulator_register(dev, &cosmo_fusb_regs[i], &cfg);
		if (IS_ERR(rdev))
			return dev_err_probe(dev, PTR_ERR(rdev),
					     "cannot register %s\n",
					     cosmo_fusb_regs[i].name);
	}

	ret = cosmo_fusb_init_chip(fusb);
	if (ret) {
		dev_err(dev, "FUSB301A init failed (%d), right port manual only\n",
			ret);
		return 0;
	}

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						cosmo_fusb_irq, IRQF_ONESHOT,
						"cosmo-fusb301a", fusb);
		if (ret)
			dev_warn(dev, "no IRQ (%d), polling only\n", ret);
	}

	queue_delayed_work(system_freezable_wq, &fusb->poll,
			   msecs_to_jiffies(FUSB_POLL_MS));
	return 0;
}

static int cosmo_fusb_resume(struct device *dev)
{
	struct cosmo_fusb *fusb = dev_get_drvdata(dev);
	int mode = cosmo_fusb_read(fusb, FUSB_MODE);

	/* The chip may have lost its setup if its supply dropped in sleep. */
	if (mode >= 0 && mode != FUSB_MODE_DRP)
		cosmo_fusb_init_chip(fusb);
	mod_delayed_work(system_freezable_wq, &fusb->poll,
			 msecs_to_jiffies(FUSB_DEBOUNCE_MS));
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(cosmo_fusb_pm, NULL, cosmo_fusb_resume);

static const struct of_device_id cosmo_fusb_of_match[] = {
	{ .compatible = "planet,cosmo-fusb301a" },
	{ }
};
MODULE_DEVICE_TABLE(of, cosmo_fusb_of_match);

static struct i2c_driver cosmo_fusb_driver = {
	.probe = cosmo_fusb_probe,
	.driver = {
		.name = "cosmo-fusb301a",
		.of_match_table = cosmo_fusb_of_match,
		.pm = pm_sleep_ptr(&cosmo_fusb_pm),
	},
};
module_i2c_driver(cosmo_fusb_driver);

MODULE_DESCRIPTION("Planet Cosmo right USB-C port (FUSB301A)");
MODULE_LICENSE("GPL");
