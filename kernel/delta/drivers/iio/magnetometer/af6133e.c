// SPDX-License-Identifier: GPL-2.0-only
/*
 * af6133e.c - Voltafield AF6133E 3-axis magnetometer
 *
 * Register sequence from the vendor Linux 4.4 driver
 * (drivers/misc/mediatek/sensors-1.0/magnetometer/af6133e-new):
 * product code 0x68 at 0x00, "golden mode" setup, single measurement
 * triggered by 0x01 to 0x0a, X/Y/Z as little-endian s16 from 0x03,
 * 0.15 uT per LSB, temperature s16 at 0x21 (0.0908 degC per LSB).
 *
 * Not ported (yet): the vendor's coil self-test (BIST), which derives
 * per-axis gain and cross-axis terms. Output here is the raw field.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>

#define AF6133E_REG_PCODE	0x00
#define AF6133E_PCODE_VAL	0x68
#define AF6133E_REG_DATA	0x03
#define AF6133E_REG_MEASURE	0x0a
#define AF6133E_MEASURE_ONE	0x01
#define AF6133E_REG_AVG		0x13
#define AF6133E_REG_SR_MODE	0x14
#define AF6133E_REG_AVG_2ND	0x15
#define AF6133E_REG_OSR		0x16
#define AF6133E_REG_OSC_FREQ	0x19
#define AF6133E_REG_TEMP	0x21
#define AF6133E_REG_XY_WAITING	0x32
#define AF6133E_REG_MAX		0x3f

/* Vendor waits 8 ms after a trigger; keep a margin. */
#define AF6133E_MEAS_US		10000

static const char * const af6133e_supply_names[] = { "vdd", "vid" };

struct af6133e_data {
	struct regmap *regmap;
	/* One measurement at a time: trigger, wait, read. */
	struct mutex lock;
	struct iio_mount_matrix orientation;
};

enum { AXIS_X, AXIS_Y, AXIS_Z };

static const struct iio_mount_matrix *
af6133e_get_mount_matrix(const struct iio_dev *indio_dev,
			 const struct iio_chan_spec *chan)
{
	struct af6133e_data *data = iio_priv(indio_dev);

	return &data->orientation;
}

static const struct iio_chan_spec_ext_info af6133e_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_DIR, af6133e_get_mount_matrix),
	{ }
};

#define AF6133E_MAGN(_axis) {					\
	.type = IIO_MAGN,					\
	.modified = 1,						\
	.channel2 = IIO_MOD_ ## _axis,				\
	.address = AXIS_ ## _axis,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.ext_info = af6133e_ext_info,				\
}

static const struct iio_chan_spec af6133e_channels[] = {
	AF6133E_MAGN(X),
	AF6133E_MAGN(Y),
	AF6133E_MAGN(Z),
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct reg_sequence af6133e_init_seq[] = {
	{ AF6133E_REG_AVG,	  0x47 },
	{ AF6133E_REG_SR_MODE,	  0x0d },
	{ AF6133E_REG_OSR,	  0x3d },
	{ AF6133E_REG_OSC_FREQ,	  0x3f },
	{ AF6133E_REG_XY_WAITING, 0x07 },
	{ AF6133E_REG_AVG_2ND,	  0x50 },
};

static int af6133e_measure(struct af6133e_data *data, s16 out[3])
{
	__le16 buf[3];
	int ret;

	ret = regmap_write(data->regmap, AF6133E_REG_MEASURE,
			   AF6133E_MEASURE_ONE);
	if (ret)
		return ret;

	fsleep(AF6133E_MEAS_US);

	ret = regmap_bulk_read(data->regmap, AF6133E_REG_DATA, buf,
			       sizeof(buf));
	if (ret)
		return ret;

	out[0] = le16_to_cpu(buf[0]);
	out[1] = le16_to_cpu(buf[1]);
	out[2] = le16_to_cpu(buf[2]);
	return 0;
}

static int af6133e_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct af6133e_data *data = iio_priv(indio_dev);
	__le16 t;
	s16 mag[3];
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->lock);
		if (chan->type == IIO_TEMP)
			ret = regmap_bulk_read(data->regmap, AF6133E_REG_TEMP,
					       &t, sizeof(t));
		else
			ret = af6133e_measure(data, mag);
		mutex_unlock(&data->lock);
		if (ret)
			return ret;

		if (chan->type == IIO_TEMP)
			*val = (s16)le16_to_cpu(t);
		else
			*val = mag[chan->address];
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_TEMP) {
			/* 0.0908 degC = 90.8 milli-degC per LSB */
			*val = 90;
			*val2 = 800000;
			return IIO_VAL_INT_PLUS_MICRO;
		}
		/* 0.15 uT = 0.0015 gauss per LSB */
		*val = 0;
		*val2 = 1500000;
		return IIO_VAL_INT_PLUS_NANO;
	}

	return -EINVAL;
}

static const struct iio_info af6133e_info = {
	.read_raw = af6133e_read_raw,
};

static const struct regmap_config af6133e_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AF6133E_REG_MAX,
};

static int af6133e_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct af6133e_data *data;
	struct iio_dev *indio_dev;
	unsigned int pcode;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	mutex_init(&data->lock);

	ret = devm_regulator_bulk_get_enable(dev,
					     ARRAY_SIZE(af6133e_supply_names),
					     af6133e_supply_names);
	if (ret)
		return dev_err_probe(dev, ret, "cannot enable supplies\n");

	ret = iio_read_mount_matrix(dev, &data->orientation);
	if (ret)
		return dev_err_probe(dev, ret, "bad mount-matrix\n");

	data->regmap = devm_regmap_init_i2c(client, &af6133e_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "regmap init failed\n");

	ret = regmap_read(data->regmap, AF6133E_REG_PCODE, &pcode);
	if (ret)
		return dev_err_probe(dev, ret, "cannot read product code\n");
	if (pcode != AF6133E_PCODE_VAL)
		return dev_err_probe(dev, -ENODEV,
				     "unexpected product code 0x%02x\n", pcode);

	ret = regmap_multi_reg_write(data->regmap, af6133e_init_seq,
				     ARRAY_SIZE(af6133e_init_seq));
	if (ret)
		return dev_err_probe(dev, ret, "init failed\n");
	fsleep(5000);

	indio_dev->name = "af6133e";
	indio_dev->info = &af6133e_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = af6133e_channels;
	indio_dev->num_channels = ARRAY_SIZE(af6133e_channels);

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id af6133e_of_match[] = {
	{ .compatible = "voltafield,af6133e" },
	{ }
};
MODULE_DEVICE_TABLE(of, af6133e_of_match);

static const struct i2c_device_id af6133e_id[] = {
	{ "af6133e" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, af6133e_id);

static struct i2c_driver af6133e_driver = {
	.driver = {
		.name = "af6133e",
		.of_match_table = af6133e_of_match,
	},
	.probe = af6133e_probe,
	.id_table = af6133e_id,
};
module_i2c_driver(af6133e_driver);

MODULE_DESCRIPTION("Voltafield AF6133E 3-axis magnetometer");
MODULE_LICENSE("GPL");
