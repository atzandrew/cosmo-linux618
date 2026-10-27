// SPDX-License-Identifier: GPL-2.0-only
/*
 * MT6358 fuel gauge (Cosmo Communicator, cosmo-linux618 docs/power-plan.md
 * S4a measurements, S4b state of charge).
 *
 * Reads the coulomb counter hardware the way the vendor 4.4 kernel does
 * (drivers/misc/mediatek/pmic/mt6358/v1/mt6358_gauge.c,
 * fgauge_read_current / fgauge_get_coulomb; docs/power-stock.md "MT6358
 * fuel gauge hardware"). The bootloader leaves the gauge running
 * (FGADC_CON0 FG_ON); this driver never turns it on or off, never resets
 * the charge counter and never calibrates.
 *
 * The only register it writes is the FGADC_CON1 read handshake, bits
 * [3:0]: 0x1 latch, wait for LATCHDATA_ST, read, 0x8 clear, wait, 0x0.
 *
 * Reported: current (+ = charging), charge counted since the counter was
 * last reset (at boot), voltage (MT6358 AUXADC BATADC), temperature (the
 * "battery-thermal" zone).
 *
 * State of charge (the vendor computes it in a closed userspace daemon, so
 * this is our own, kept simple):
 *  - at probe: open-circuit voltage = V - I x R (battery node
 *    factory-internal-resistance), through the battery node's OCV table;
 *  - then the counted charge moves it, against "cosmo,charge-full-
 *    microamp-hours" (usable capacity);
 *  - anchors: 100 % when the charger reports charge done; 0 % below
 *    3.40 V and at most 3 % below 3.45 V (open-circuit estimate);
 *  - 100 % is only shown once the charger reports done (99 % before).
 */

#include <linux/iio/consumer.h>
#include <linux/kernel.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/sort.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/devm-helpers.h>

/* Vendor upmu_hw.h (mt6771) */
#define MT6358_FGADC_CON0		0x0d08
#define MT6358_FGADC_CON1		0x0d0a
#define MT6358_FGADC_CAR_CON0		0x0d14
#define MT6358_FGADC_CAR_CON1		0x0d16
#define MT6358_FGADC_CUR_CON0		0x0d8a

#define FG_ON				BIT(0)		/* FGADC_CON0 */
#define FG_CMD_MASK			GENMASK(3, 0)	/* FGADC_CON1 */
#define FG_CMD_LATCH			0x1
#define FG_CMD_CLEAR			0x8
#define FG_LATCHDATA_ST			BIT(15)

/* Vendor mt6358_gauge.c units */
#define UNIT_FGCURRENT_NA		381470	/* 381.470 uA per LSB */
#define UNIT_FGCAR_NAH			108507	/* 190.735 uAs x 2^11 / 3600 */
#define CAR_TUNE_DEFAULT		1000	/* x 1/1000; vendor DT: 930 */

/* MT6358 BATADC: 15 bit; mt6359-auxadc reports full scale in mV */
#define BATADC_BITS			15

/* State of charge */
#define SOC_POLL_MS			20000
#define SOC_EMPTY_UV			3400000	/* vendor shutdown voltage */
#define SOC_LOW_UV			3450000
#define SOC_LOW_CAP			3	/* % */
#define SOC_START_SAMPLES		5
#define R_INT_DEFAULT_UOHM		171000	/* median of the 25 C profile */

struct mt6358_gauge {
	struct device *dev;
	struct regmap *map;
	struct mutex lock;		/* one FGADC_CON1 handshake at a time */
	struct power_supply *psy;
	struct power_supply *charger;
	struct iio_channel *batadc;
	struct thermal_zone_device *batt_tz;
	struct power_supply_battery_info *info;
	u32 car_tune;
	int charge_full_design_uah;
	int voltage_max_design_uv;
	int voltage_min_design_uv;

	/* State of charge, under soc_lock */
	struct mutex soc_lock;
	struct delayed_work soc_dwork;
	u32 full_uah;
	int r_int_uohm;
	s64 anchor_car_uah;		/* counter value at the anchor */
	s64 anchor_rem_uah;		/* remaining charge at the anchor */
	bool charger_done;
	bool ready;			/* start value set */
	int last_capacity;
};

static int mt6358_gauge_wait(struct mt6358_gauge *g, bool set)
{
	unsigned int val;

	return regmap_read_poll_timeout(g->map, MT6358_FGADC_CON1, val,
					!!(val & FG_LATCHDATA_ST) == set,
					10, 10000);
}

/* Latch and read current and charge in one handshake (vendor steps 1-8) */
static int mt6358_gauge_latch(struct mt6358_gauge *g, unsigned int *cur,
			      unsigned int *car_lo, unsigned int *car_hi)
{
	int ret, ret2;

	mutex_lock(&g->lock);

	ret = regmap_update_bits(g->map, MT6358_FGADC_CON1, FG_CMD_MASK,
				 FG_CMD_LATCH);
	if (ret)
		goto out;

	ret = mt6358_gauge_wait(g, true);
	if (!ret)
		ret = regmap_read(g->map, MT6358_FGADC_CUR_CON0, cur);
	if (!ret)
		ret = regmap_read(g->map, MT6358_FGADC_CAR_CON0, car_lo);
	if (!ret)
		ret = regmap_read(g->map, MT6358_FGADC_CAR_CON1, car_hi);

	/* Always hand the latch back, even after a timeout */
	ret2 = regmap_update_bits(g->map, MT6358_FGADC_CON1, FG_CMD_MASK,
				  FG_CMD_CLEAR);
	if (!ret2)
		ret2 = mt6358_gauge_wait(g, false);
	if (!ret2 || ret2 == -ETIMEDOUT)
		ret2 = regmap_update_bits(g->map, MT6358_FGADC_CON1,
					  FG_CMD_MASK, 0);
	if (!ret)
		ret = ret2;
out:
	mutex_unlock(&g->lock);
	if (ret)
		dev_warn_ratelimited(g->dev, "gauge read failed: %d\n", ret);
	return ret;
}

/* Vendor fgauge_read_current: raw > 32767 is discharge, 65535 - raw */
static s64 mt6358_gauge_cur_ua(struct mt6358_gauge *g, unsigned int raw)
{
	s64 lsb;

	raw &= 0xffff;
	if (raw > 32767)
		lsb = -(s64)(65535 - raw);
	else
		lsb = raw;

	return div_s64(lsb * UNIT_FGCURRENT_NA * g->car_tune, 1000 * 1000);
}

/* Vendor fgauge_get_coulomb: 20 bits + sign, discharge is 0xfffff - car */
static s64 mt6358_gauge_car_uah(struct mt6358_gauge *g, unsigned int lo,
				unsigned int hi)
{
	u32 car = ((lo & 0xffff) >> 11) | ((hi & 0x7fff) << 5);
	s64 lsb;

	if (car == 0 || car == 0xfffff)
		lsb = 0;
	else if (hi & BIT(15))
		lsb = -(s64)(0xfffff - car);
	else
		lsb = car;

	return div_s64(lsb * UNIT_FGCAR_NAH * g->car_tune, 1000 * 1000);
}

static int mt6358_gauge_read_ic(struct mt6358_gauge *g, s64 *ua, s64 *car)
{
	unsigned int cur, lo, hi;
	int ret;

	ret = mt6358_gauge_latch(g, &cur, &lo, &hi);
	if (ret)
		return ret;
	if (ua)
		*ua = mt6358_gauge_cur_ua(g, cur);
	if (car)
		*car = mt6358_gauge_car_uah(g, lo, hi);
	return 0;
}

static int mt6358_gauge_voltage_uv(struct mt6358_gauge *g, int *uv)
{
	int raw, scale, scale2, ret;

	ret = iio_read_channel_raw(g->batadc, &raw);
	if (ret < 0)
		return ret;
	ret = iio_read_channel_scale(g->batadc, &scale, &scale2);
	if (ret != IIO_VAL_INT)
		return ret < 0 ? ret : -EINVAL;

	*uv = (int)(((s64)raw * scale * 1000) >> BATADC_BITS);
	return 0;
}

/* Open-circuit estimate: V - I x R (I > 0 charging) */
static int mt6358_gauge_ocv_uv(struct mt6358_gauge *g, int uv, s64 ua)
{
	return uv - (int)div_s64(ua * g->r_int_uohm, 1000000);
}

static int mt6358_gauge_ocv2pct(struct mt6358_gauge *g, int ocv_uv)
{
	int pct = power_supply_batinfo_ocv2cap(g->info, ocv_uv, 25);

	return clamp(pct, 0, 100);
}

/* Caller holds soc_lock */
static void mt6358_gauge_anchor(struct mt6358_gauge *g, s64 rem_uah,
				s64 car_uah, const char *why)
{
	g->anchor_rem_uah = clamp_t(s64, rem_uah, 0, g->full_uah);
	g->anchor_car_uah = car_uah;
	dev_info(g->dev, "state of charge %lld %% (%s)\n",
		 div_s64(g->anchor_rem_uah * 100, g->full_uah), why);
}

/* Caller holds soc_lock. Keeps the anchor inside 0..full. */
static s64 mt6358_gauge_rem_uah(struct mt6358_gauge *g, s64 car_uah)
{
	s64 rem = g->anchor_rem_uah + (car_uah - g->anchor_car_uah);

	if (rem > g->full_uah || rem < 0) {
		rem = clamp_t(s64, rem, 0, g->full_uah);
		g->anchor_rem_uah = rem;
		g->anchor_car_uah = car_uah;
	}
	return rem;
}

/* Caller holds soc_lock */
static int mt6358_gauge_capacity(struct mt6358_gauge *g, s64 rem_uah)
{
	int cap = (int)div_s64(rem_uah * 100 + g->full_uah / 2, g->full_uah);

	cap = clamp(cap, 0, 100);
	if (cap == 100 && !g->charger_done)
		cap = 99;
	return cap;
}

static int mt6358_gauge_charger_status(struct mt6358_gauge *g)
{
	union power_supply_propval val;

	if (!g->charger)
		g->charger = power_supply_get_by_name("mt6370-charger");
	if (!g->charger)
		return POWER_SUPPLY_STATUS_UNKNOWN;
	if (power_supply_get_property(g->charger, POWER_SUPPLY_PROP_STATUS,
				      &val))
		return POWER_SUPPLY_STATUS_UNKNOWN;
	return val.intval;
}

static void mt6358_gauge_soc_update(struct mt6358_gauge *g)
{
	bool done = mt6358_gauge_charger_status(g) == POWER_SUPPLY_STATUS_FULL;
	s64 ua, car, rem;
	int uv, ocv, cap;
	bool changed;

	if (!READ_ONCE(g->ready))
		return;
	if (mt6358_gauge_read_ic(g, &ua, &car) ||
	    mt6358_gauge_voltage_uv(g, &uv))
		return;
	ocv = mt6358_gauge_ocv_uv(g, uv, ua);

	mutex_lock(&g->soc_lock);
	if (done && !g->charger_done)
		mt6358_gauge_anchor(g, g->full_uah, car, "charge done");
	g->charger_done = done;

	rem = mt6358_gauge_rem_uah(g, car);
	if (ocv < SOC_EMPTY_UV && rem > 0) {
		mt6358_gauge_anchor(g, 0, car, "below 3.40 V");
		rem = 0;
	} else if (ocv < SOC_LOW_UV &&
		   rem > (s64)g->full_uah * SOC_LOW_CAP / 100) {
		mt6358_gauge_anchor(g, (s64)g->full_uah * SOC_LOW_CAP / 100,
				    car, "below 3.45 V");
		rem = g->anchor_rem_uah;
	}

	cap = mt6358_gauge_capacity(g, rem);
	changed = cap != g->last_capacity;
	g->last_capacity = cap;
	mutex_unlock(&g->soc_lock);

	if (changed)
		power_supply_changed(g->psy);
}

static void mt6358_gauge_soc_dwork_func(struct work_struct *work)
{
	struct mt6358_gauge *g = container_of(work, struct mt6358_gauge,
					      soc_dwork.work);

	mt6358_gauge_soc_update(g);
	schedule_delayed_work(&g->soc_dwork, msecs_to_jiffies(SOC_POLL_MS));
}

static int mt6358_gauge_cmp_int(const void *a, const void *b)
{
	return *(const int *)a - *(const int *)b;
}

/* Start value: median open-circuit estimate of a few samples */
static int mt6358_gauge_soc_start(struct mt6358_gauge *g)
{
	int ocv[SOC_START_SAMPLES], uv, i, ret, pct;
	s64 ua, car = 0;

	for (i = 0; i < SOC_START_SAMPLES; i++) {
		ret = mt6358_gauge_read_ic(g, &ua, &car);
		if (!ret)
			ret = mt6358_gauge_voltage_uv(g, &uv);
		if (ret)
			return ret;
		ocv[i] = mt6358_gauge_ocv_uv(g, uv, ua);
		if (i + 1 < SOC_START_SAMPLES)
			msleep(20);
	}
	sort(ocv, SOC_START_SAMPLES, sizeof(int), mt6358_gauge_cmp_int, NULL);
	pct = mt6358_gauge_ocv2pct(g, ocv[SOC_START_SAMPLES / 2]);

	mutex_lock(&g->soc_lock);
	dev_info(g->dev, "start: OCV %d uV (V %d uV, I %lld uA, R %d uOhm)\n",
		 ocv[SOC_START_SAMPLES / 2], uv, ua, g->r_int_uohm);
	mt6358_gauge_anchor(g, (s64)g->full_uah * pct / 100, car, "OCV table");
	g->last_capacity = mt6358_gauge_capacity(g, g->anchor_rem_uah);
	WRITE_ONCE(g->ready, true);
	mutex_unlock(&g->soc_lock);
	return 0;
}

static enum power_supply_property mt6358_gauge_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
};

static int mt6358_gauge_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	struct mt6358_gauge *g = power_supply_get_drvdata(psy);
	s64 ua, car, rem;
	int ret, temp;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (mt6358_gauge_charger_status(g) == POWER_SUPPLY_STATUS_FULL) {
			val->intval = POWER_SUPPLY_STATUS_FULL;
			return 0;
		}
		ret = mt6358_gauge_read_ic(g, &ua, NULL);
		if (ret)
			return ret;
		if (power_supply_am_i_supplied(psy) <= 0)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (ua > 0)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		return 0;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return mt6358_gauge_voltage_uv(g, &val->intval);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = mt6358_gauge_read_ic(g, &ua, NULL);
		if (ret)
			return ret;
		val->intval = ua;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		ret = mt6358_gauge_read_ic(g, NULL, &car);
		if (ret)
			return ret;
		val->intval = car;
		return 0;
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		if (!READ_ONCE(g->ready))
			return -ENODATA;
		ret = mt6358_gauge_read_ic(g, NULL, &car);
		if (ret)
			return ret;
		mutex_lock(&g->soc_lock);
		rem = mt6358_gauge_rem_uah(g, car);
		if (psp == POWER_SUPPLY_PROP_CAPACITY)
			val->intval = mt6358_gauge_capacity(g, rem);
		else
			val->intval = rem;
		mutex_unlock(&g->soc_lock);
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		if (!READ_ONCE(g->ready))
			return -ENODATA;
		val->intval = g->full_uah;
		return 0;
	case POWER_SUPPLY_PROP_TEMP:
		ret = thermal_zone_get_temp(g->batt_tz, &temp);
		if (ret)
			return ret;
		val->intval = temp / 100;	/* 0.1 C */
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = g->charge_full_design_uah;
		return val->intval > 0 ? 0 : -ENODATA;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = g->voltage_max_design_uv;
		return val->intval > 0 ? 0 : -ENODATA;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = g->voltage_min_design_uv;
		return val->intval > 0 ? 0 : -ENODATA;
	default:
		return -EINVAL;
	}
}

static void mt6358_gauge_external_power_changed(struct power_supply *psy)
{
	struct mt6358_gauge *g = power_supply_get_drvdata(psy);

	/* Charger events (plug, done) re-run the update soon */
	mod_delayed_work(system_wq, &g->soc_dwork, msecs_to_jiffies(500));
	power_supply_changed(psy);
}

static const struct power_supply_desc mt6358_gauge_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = mt6358_gauge_props,
	.num_properties = ARRAY_SIZE(mt6358_gauge_props),
	.get_property = mt6358_gauge_get_property,
	.external_power_changed = mt6358_gauge_external_power_changed,
};

static void mt6358_gauge_put_charger(void *data)
{
	struct mt6358_gauge *g = data;

	if (g->charger)
		power_supply_put(g->charger);
}

static int mt6358_gauge_probe(struct platform_device *pdev)
{
	struct mt6397_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct power_supply_config cfg = {};
	struct mt6358_gauge *g;
	unsigned int con0;
	s64 ua, car;
	int ret;

	if (!chip || chip->chip_id != MT6358_CHIP_ID)
		return dev_err_probe(dev, -ENODEV, "not an MT6358\n");

	g = devm_kzalloc(dev, sizeof(*g), GFP_KERNEL);
	if (!g)
		return -ENOMEM;

	g->dev = dev;
	g->map = chip->regmap;
	ret = devm_mutex_init(dev, &g->lock);
	if (!ret)
		ret = devm_mutex_init(dev, &g->soc_lock);
	if (ret)
		return ret;

	if (device_property_read_u32(dev, "mediatek,car-tune", &g->car_tune))
		g->car_tune = CAR_TUNE_DEFAULT;

	g->batadc = devm_iio_channel_get(dev, "batadc");
	if (IS_ERR(g->batadc))
		return dev_err_probe(dev, PTR_ERR(g->batadc),
				     "no batadc channel\n");

	g->batt_tz = thermal_zone_get_zone_by_name("battery-thermal");
	if (IS_ERR(g->batt_tz))
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "waiting for thermal zone battery-thermal\n");

	ret = regmap_read(g->map, MT6358_FGADC_CON0, &con0);
	if (ret)
		return dev_err_probe(dev, ret, "cannot read FGADC_CON0\n");
	if (!(con0 & FG_ON))
		return dev_err_probe(dev, -ENODEV,
				     "gauge is off (FGADC_CON0 0x%04x); not turning it on\n",
				     con0);

	platform_set_drvdata(pdev, g);
	ret = devm_add_action_or_reset(dev, mt6358_gauge_put_charger, g);
	if (ret)
		return ret;
	ret = devm_delayed_work_autocancel(dev, &g->soc_dwork,
					   mt6358_gauge_soc_dwork_func);
	if (ret)
		return ret;

	cfg.drv_data = g;
	cfg.fwnode = dev_fwnode(dev);

	g->psy = devm_power_supply_register(dev, &mt6358_gauge_desc, &cfg);
	if (IS_ERR(g->psy))
		return dev_err_probe(dev, PTR_ERR(g->psy),
				     "cannot register power supply\n");

	ret = power_supply_get_battery_info(g->psy, &g->info);
	if (ret)
		return dev_err_probe(dev, ret, "no monitored-battery\n");
	if (!g->info->ocv_table_size[0])
		return dev_err_probe(dev, -EINVAL, "battery node has no OCV table\n");

	g->charge_full_design_uah = g->info->charge_full_design_uah;
	g->voltage_max_design_uv = g->info->constant_charge_voltage_max_uv;
	g->voltage_min_design_uv = g->info->voltage_min_design_uv;
	g->r_int_uohm = g->info->factory_internal_resistance_uohm > 0 ?
			g->info->factory_internal_resistance_uohm :
			R_INT_DEFAULT_UOHM;
	if (device_property_read_u32(dev, "cosmo,charge-full-microamp-hours",
				     &g->full_uah))
		g->full_uah = g->charge_full_design_uah;
	if (!g->full_uah)
		return dev_err_probe(dev, -EINVAL, "no usable capacity\n");

	ret = mt6358_gauge_read_ic(g, &ua, &car);
	if (ret)
		return dev_err_probe(dev, ret, "first gauge read failed\n");

	dev_info(dev, "FGADC_CON0 0x%04x, car-tune %u: current %lld uA, charge %lld uAh, full %u uAh\n",
		 con0, g->car_tune, ua, car, g->full_uah);

	ret = mt6358_gauge_soc_start(g);
	if (ret)
		return dev_err_probe(dev, ret, "start value failed\n");

	schedule_delayed_work(&g->soc_dwork, msecs_to_jiffies(SOC_POLL_MS));
	return 0;
}

static const struct of_device_id mt6358_gauge_of_match[] = {
	{ .compatible = "mediatek,mt6358-gauge" },
	{}
};
MODULE_DEVICE_TABLE(of, mt6358_gauge_of_match);

static struct platform_driver mt6358_gauge_driver = {
	.probe = mt6358_gauge_probe,
	.driver = {
		.name = "mt6358-gauge",
		.of_match_table = mt6358_gauge_of_match,
	},
};
module_platform_driver(mt6358_gauge_driver);

MODULE_DESCRIPTION("MediaTek MT6358 fuel gauge");
MODULE_LICENSE("GPL");
