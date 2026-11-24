// SPDX-License-Identifier: GPL-2.0
/*
 * MT6358 ACCDET: headset plug detection on the PMIC-internal EINT0 input.
 *
 * Planet Computers Cosmo Communicator. The register sequence follows the
 * vendor 4.4 driver drivers/misc/mediatek/accdet/mt6771/accdet.c as built
 * for this device (CONFIG_ACCDET_EINT_IRQ + CONFIG_ACCDET_SUPPORT_EINT0,
 * SW mode, level-low EINT, dtbo fragment@13 values). Mainline has only the
 * MT6359 ACCDET, whose register map differs.
 *
 * EINT0 reports plug-in/plug-out. On plug-in the ACCDET unit is enabled
 * (as the vendor driver does): it connects MICBIAS1 to the headset mic and
 * its comparator state tells a headset (AB = 01) from headphones (AB = 00).
 * With headphones only, the unit is switched off again after 6 s (vendor
 * micbias_timer). Headset buttons are not handled yet.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <sound/jack.h>
#include <sound/soc.h>

#include "mt6358-accdet.h"

/* Registers (vendor reg_accdet.h) */
#define AUD_TOP_RST_CON0	0x2220
#define AUDENC_ANA_CON6		0x2394
#define AUDENC_ANA_CON10	0x239c
#define AUDENC_ANA_CON11	0x239e
#define ACCDET_CON01		0x250a	/* ACCDET_CTRL */
#define ACCDET_CON02		0x250c	/* STATE_SWCTRL */
#define ACCDET_CON03		0x250e	/* PWM_WIDTH */
#define ACCDET_CON04		0x2510	/* PWM_THRESH */
#define ACCDET_CON05		0x2512	/* EN_DELAY_NUM */
#define ACCDET_CON06		0x2514	/* DEBOUNCE0 */
#define ACCDET_CON07		0x2516	/* DEBOUNCE1 */
#define ACCDET_CON09		0x251a	/* DEBOUNCE3 */
#define ACCDET_CON10		0x251c	/* DEBOUNCE4 (auxadc) */
#define ACCDET_CON12		0x2520	/* IRQ_STATUS */
#define ACCDET_CON13		0x2522	/* SW_CONTROL */
#define ACCDET_CON14		0x2524	/* STATE_RG (RO) */
#define ACCDET_CON15		0x2526	/* EINT0_CONTROL */
#define ACCDET_CON24		0x2538	/* HW_SET */

#define RG_ACCDET_RST			BIT(1)
#define RG_FSTDSCHRG_BITS		(BIT(5) | BIT(6))
#define RG_AUD_MICBIAS1_LOWP_EN		BIT(2)
#define ANA_CON10_KEEP			0xff8e
#define ANA_CON11_MODE1			0x0807	/* ACC mode + EINT config ACCDET */
#define ANA_CON11_KEEP			0xff7f

#define ACCDET_EN			BIT(0)
#define ACCDET_SEQ_INIT			BIT(1)
#define ACCDET_EINT0_EN			BIT(2)

#define ACCDET_SWCTRL_EN		0x0007	/* CMP, VTH, MICBIAS PWM enable */
#define ACCDET_EINT0_PWM_EN		BIT(3)
#define ACCDET_PWM_IDLE			(0x07 << 8)
#define ACCDET_EINT0_PWM_IDLE		BIT(11)

#define ACCDET_IRQ			BIT(0)
#define ACCDET_EINT0_IRQ		BIT(2)
#define ACCDET_IRQ_CLR			BIT(8)
#define ACCDET_EINT0_IRQ_CLR		BIT(10)
#define ACCDET_EINT_IRQ_CLR		(0x03 << 10)
#define ACCDET_EINT0_IRQ_POL		BIT(14)

#define ACCDET_STATE_MEM_SHIFT		6
#define ACCDET_STATE_AB_MASK		0x03
#define ACCDET_STATE_AB_00		0x00	/* headphones (3-pole) */
#define ACCDET_STATE_AB_01		0x01	/* headset with mic */
#define ACCDET_STATE_AB_11		0x03	/* open */

#define ACCDET_EINT0_DEB_MASK		(0x0f << 3)
#define ACCDET_EINT0_DEB_IN_256		(0x0e << 3)
#define ACCDET_EINT0_DEB_OUT_012	(0x01 << 3)
#define ACCDET_EINT0_PWM_MASK		(0x1f << 8)
#define ACCDET_EINT0_PWM_THRSH		(0x06 << 8)
#define ACCDET_EINT0_PWM_WIDTH		(0x02 << 12)

#define ACCDET_HWMODE_SEL		BIT(2)
#define ACCDET_FAST_DISCHARGE		BIT(4)

/* dtbo fragment@13 headset-mode-setting <0x500 0x500 1 0x1f0 0x800 0x800 0x20 0x44> */
#define PWM_WIDTH	0x500
#define PWM_THRESH	0x500
#define FALL_DELAY	1
#define RISE_DELAY	0x1f0
#define DEBOUNCE0	0x800
#define DEBOUNCE1	0x800
#define DEBOUNCE3	0x20
#define DEBOUNCE4	0x44
#define BUTTON_DEBOUNCE	(DEBOUNCE0 >> 1)	/* s_button_press_debounce */
#define MICBIAS_VOL	6	/* accdet-mic-vol: 2.6 V */
#define MICBIAS_OFF_MS	6000	/* MICBIAS_DISABLE_TIMER */

struct mt6358_accdet {
	struct device *dev;
	struct regmap *regmap;
	struct snd_soc_jack *jack;
	struct mutex lock;
	struct delayed_work micbias_work;
	bool plugged;
	bool unit_on;
	int jack_type;		/* 0, SND_JACK_HEADPHONE or SND_JACK_HEADSET */
	int eint_irq;
	int accdet_irq;
};

static void mt6358_accdet_set_debounce(struct mt6358_accdet *priv)
{
	regmap_write(priv->regmap, ACCDET_CON06, DEBOUNCE0);
	regmap_write(priv->regmap, ACCDET_CON07, DEBOUNCE1);
	regmap_write(priv->regmap, ACCDET_CON09, DEBOUNCE3);
	regmap_write(priv->regmap, ACCDET_CON10, DEBOUNCE4);
}

/* vendor accdet_init(): sequence init pulse + debounce times */
static void mt6358_accdet_seq_init(struct mt6358_accdet *priv)
{
	regmap_update_bits(priv->regmap, ACCDET_CON01, ACCDET_SEQ_INIT,
			   ACCDET_SEQ_INIT);
	mdelay(2);
	regmap_update_bits(priv->regmap, ACCDET_CON01, ACCDET_SEQ_INIT, 0);
	mdelay(1);
	mt6358_accdet_set_debounce(priv);
}

/* accdet_init() + accdet_init_once(0/1) + accdet_eint_high_level_support() */
static void mt6358_accdet_init_hw(struct mt6358_accdet *priv)
{
	struct regmap *map = priv->regmap;

	/* Reset the ACCDET block first, so every value below survives. */
	regmap_update_bits(map, AUD_TOP_RST_CON0, RG_ACCDET_RST, RG_ACCDET_RST);
	regmap_update_bits(map, AUD_TOP_RST_CON0, RG_ACCDET_RST, 0);

	/* Stock leaves the SW control register at 0 (Gemian idle dump). */
	regmap_write(map, ACCDET_CON13, 0);

	/* PWM frequency, duty, rise/fall delay */
	regmap_write(map, ACCDET_CON04, PWM_THRESH - 1);
	regmap_write(map, ACCDET_CON03, PWM_WIDTH - 1);
	regmap_write(map, ACCDET_CON05, FALL_DELAY << 15 | RISE_DELAY);

	/* SW mode, digital fast discharge; analog fast discharge (level low) */
	regmap_update_bits(map, ACCDET_CON24,
			   ACCDET_HWMODE_SEL | ACCDET_FAST_DISCHARGE,
			   ACCDET_FAST_DISCHARGE);
	regmap_update_bits(map, AUDENC_ANA_CON6, RG_FSTDSCHRG_BITS,
			   RG_FSTDSCHRG_BITS);

	mt6358_accdet_seq_init(priv);

	/* Analog: MICBIAS1 voltage + low-power (not powered), ACC mode 1 */
	regmap_update_bits(map, AUDENC_ANA_CON10,
			   (~ANA_CON10_KEEP & 0xffff) | RG_AUD_MICBIAS1_LOWP_EN,
			   MICBIAS_VOL << 4 | RG_AUD_MICBIAS1_LOWP_EN);
	regmap_update_bits(map, AUDENC_ANA_CON11,
			   (~ANA_CON11_KEEP & 0xffff) | ANA_CON11_MODE1,
			   ANA_CON11_MODE1);

	/* EINT0 PWM, enable EINT0 */
	regmap_update_bits(map, ACCDET_CON15, ACCDET_EINT0_PWM_MASK,
			   ACCDET_EINT0_PWM_THRSH);
	regmap_update_bits(map, ACCDET_CON15, ACCDET_EINT0_PWM_WIDTH,
			   ACCDET_EINT0_PWM_WIDTH);
	regmap_update_bits(map, ACCDET_CON02,
			   ACCDET_EINT0_PWM_EN | ACCDET_EINT0_PWM_IDLE,
			   ACCDET_EINT0_PWM_EN | ACCDET_EINT0_PWM_IDLE);
	regmap_update_bits(map, ACCDET_CON01, ACCDET_EINT0_EN, ACCDET_EINT0_EN);

	/* Level-low EINT: wait for plug-in, 256 ms debounce */
	regmap_update_bits(map, ACCDET_CON12, ACCDET_EINT0_IRQ_POL, 0);
	regmap_update_bits(map, ACCDET_CON15, ACCDET_EINT0_DEB_MASK,
			   ACCDET_EINT0_DEB_IN_256);

	priv->plugged = false;
	priv->unit_on = false;
	priv->jack_type = 0;
}

/* vendor plug-in: accdet_init(), PWM idle on, enable_accdet(EINT0_PWM_IDLE) */
static void mt6358_accdet_unit_enable(struct mt6358_accdet *priv)
{
	struct regmap *map = priv->regmap;

	mt6358_accdet_seq_init(priv);
	regmap_update_bits(map, ACCDET_CON02, ACCDET_PWM_IDLE, ACCDET_PWM_IDLE);
	regmap_update_bits(map, AUDENC_ANA_CON10, RG_AUD_MICBIAS1_LOWP_EN,
			   RG_AUD_MICBIAS1_LOWP_EN);
	regmap_update_bits(map, ACCDET_CON02,
			   ACCDET_EINT0_PWM_IDLE | ACCDET_SWCTRL_EN,
			   ACCDET_EINT0_PWM_IDLE | ACCDET_SWCTRL_EN);
	regmap_update_bits(map, ACCDET_CON01, ACCDET_EN, ACCDET_EN);
	priv->unit_on = true;
}

/* vendor disable_accdet() */
static void mt6358_accdet_unit_disable(struct mt6358_accdet *priv)
{
	struct regmap *map = priv->regmap;
	unsigned int val;

	regmap_update_bits(map, ACCDET_CON12, ACCDET_IRQ_CLR, ACCDET_IRQ_CLR);
	udelay(200);
	if (regmap_read_poll_timeout(map, ACCDET_CON12, val,
				     !(val & ACCDET_IRQ), 100, 20000))
		dev_warn(priv->dev, "ACCDET IRQ did not clear (0x%x)\n", val);
	regmap_update_bits(map, ACCDET_CON12, ACCDET_IRQ_CLR, 0);
	regmap_update_bits(map, ACCDET_CON01, ACCDET_EN, 0);
	regmap_update_bits(map, ACCDET_CON02, ACCDET_SWCTRL_EN, 0);
	priv->unit_on = false;
}

static void mt6358_accdet_report(struct mt6358_accdet *priv)
{
	if (priv->jack)
		snd_soc_jack_report(priv->jack, priv->jack_type,
				    SND_JACK_HEADSET);
}

/* Headphones only: the vendor driver turns the unit off after 6 s. */
static void mt6358_accdet_micbias_work(struct work_struct *work)
{
	struct mt6358_accdet *priv = container_of(to_delayed_work(work),
						  struct mt6358_accdet,
						  micbias_work);

	mutex_lock(&priv->lock);
	if (priv->plugged && priv->unit_on &&
	    priv->jack_type != SND_JACK_HEADSET) {
		mt6358_accdet_unit_disable(priv);
		dev_info(priv->dev, "headphones (no mic): ACCDET unit off\n");
	}
	mutex_unlock(&priv->lock);
}

/* accdet_irq_handler() EINT0 branch + accdet_eint_func() + eint work */
static irqreturn_t mt6358_accdet_eint0_irq(int irq, void *data)
{
	struct mt6358_accdet *priv = data;
	struct regmap *map = priv->regmap;
	unsigned int val;
	int ret;

	mutex_lock(&priv->lock);

	regmap_read(map, ACCDET_CON12, &val);
	if (!(val & ACCDET_EINT0_IRQ)) {
		mutex_unlock(&priv->lock);
		return IRQ_NONE;
	}

	/* Level low: plugged waits for high (unplug), unplugged for low. */
	regmap_update_bits(map, ACCDET_CON12, ACCDET_EINT0_IRQ_POL,
			   priv->plugged ? 0 : ACCDET_EINT0_IRQ_POL);

	regmap_update_bits(map, ACCDET_CON12, ACCDET_EINT0_IRQ_CLR,
			   ACCDET_EINT0_IRQ_CLR);
	ret = regmap_read_poll_timeout(map, ACCDET_CON12, val,
				       !(val & ACCDET_EINT0_IRQ), 10, 5000);
	if (ret)
		dev_warn(priv->dev, "EINT0 status did not clear (0x%x)\n", val);
	regmap_update_bits(map, ACCDET_CON12, ACCDET_EINT0_IRQ_CLR, 0);

	if (priv->plugged) {
		/* plug-out: 256 ms debounce for the next plug-in */
		regmap_update_bits(map, ACCDET_CON15, ACCDET_EINT0_DEB_MASK,
				   ACCDET_EINT0_DEB_IN_256);
		regmap_write(map, ACCDET_CON09, DEBOUNCE3);
		priv->plugged = false;
		cancel_delayed_work(&priv->micbias_work);

		regmap_update_bits(map, ACCDET_CON02, ACCDET_PWM_IDLE, 0);
		if (priv->unit_on)
			mt6358_accdet_unit_disable(priv);
		regmap_update_bits(map, ACCDET_CON12, ACCDET_EINT_IRQ_CLR, 0);
		priv->jack_type = 0;
	} else {
		/* plug-in: 0.12 ms debounce for the plug-out */
		regmap_update_bits(map, ACCDET_CON15, ACCDET_EINT0_DEB_MASK,
				   ACCDET_EINT0_DEB_OUT_012);
		priv->plugged = true;
		/* headphones until the ACCDET unit says headset */
		priv->jack_type = SND_JACK_HEADPHONE;
		mt6358_accdet_unit_enable(priv);
		schedule_delayed_work(&priv->micbias_work,
				      msecs_to_jiffies(MICBIAS_OFF_MS));
	}

	dev_info(priv->dev, "jack %s\n", priv->plugged ? "plugged" : "unplugged");
	mt6358_accdet_report(priv);

	mutex_unlock(&priv->lock);
	return IRQ_HANDLED;
}

/* accdet_irq_handler() ACCDET branch + check_cable_type() (no buttons) */
static irqreturn_t mt6358_accdet_irq(int irq, void *data)
{
	struct mt6358_accdet *priv = data;
	struct regmap *map = priv->regmap;
	unsigned int val, state;

	mutex_lock(&priv->lock);

	regmap_read(map, ACCDET_CON12, &val);
	if (!(val & ACCDET_IRQ)) {
		mutex_unlock(&priv->lock);
		return IRQ_NONE;
	}

	regmap_update_bits(map, ACCDET_CON12, ACCDET_IRQ_CLR, ACCDET_IRQ_CLR);
	regmap_read(map, ACCDET_CON14, &val);
	state = (val >> ACCDET_STATE_MEM_SHIFT) & ACCDET_STATE_AB_MASK;
	if (regmap_read_poll_timeout(map, ACCDET_CON12, val,
				     !(val & ACCDET_IRQ), 10, 5000))
		dev_warn(priv->dev, "ACCDET IRQ did not clear (0x%x)\n", val);
	regmap_update_bits(map, ACCDET_CON12, ACCDET_IRQ_CLR, 0);

	dev_dbg(priv->dev, "ACCDET state AB=%u\n", state);

	if (priv->plugged && priv->unit_on && state == ACCDET_STATE_AB_01 &&
	    priv->jack_type != SND_JACK_HEADSET) {
		/* headset: ABC=110 debounce x30, button debounce (vendor) */
		regmap_write(map, ACCDET_CON09, DEBOUNCE3 * 30);
		regmap_write(map, ACCDET_CON06, BUTTON_DEBOUNCE);
		priv->jack_type = SND_JACK_HEADSET;
		dev_info(priv->dev, "headset with mic\n");
		mt6358_accdet_report(priv);
	}

	mutex_unlock(&priv->lock);
	return IRQ_HANDLED;
}

int mt6358_accdet_enable_jack_detect(struct snd_soc_component *component,
				     struct snd_soc_jack *jack)
{
	struct mt6358_accdet *priv = snd_soc_component_get_drvdata(component);

	mutex_lock(&priv->lock);
	priv->jack = jack;
	mt6358_accdet_report(priv);
	mutex_unlock(&priv->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(mt6358_accdet_enable_jack_detect);

static const struct snd_soc_component_driver mt6358_accdet_component = {
	.name = "mt6358-accdet",
};

static int mt6358_accdet_probe(struct platform_device *pdev)
{
	struct mt6397_chip *mt6397 = dev_get_drvdata(pdev->dev.parent);
	struct mt6358_accdet *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->regmap = mt6397->regmap;
	mutex_init(&priv->lock);
	INIT_DELAYED_WORK(&priv->micbias_work, mt6358_accdet_micbias_work);
	platform_set_drvdata(pdev, priv);

	priv->accdet_irq = platform_get_irq_byname(pdev, "accdet_irq");
	if (priv->accdet_irq < 0)
		return priv->accdet_irq;
	priv->eint_irq = platform_get_irq_byname(pdev, "accdet_eint0");
	if (priv->eint_irq < 0)
		return priv->eint_irq;

	mt6358_accdet_init_hw(priv);

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &mt6358_accdet_component,
					      NULL, 0);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register component\n");

	ret = devm_request_threaded_irq(&pdev->dev, priv->accdet_irq, NULL,
					mt6358_accdet_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"ACCDET", priv);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to request ACCDET irq\n");

	ret = devm_request_threaded_irq(&pdev->dev, priv->eint_irq, NULL,
					mt6358_accdet_eint0_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"ACCDET_EINT0", priv);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to request EINT0 irq\n");

	return 0;
}

static void mt6358_accdet_remove(struct platform_device *pdev)
{
	struct mt6358_accdet *priv = platform_get_drvdata(pdev);

	disable_irq(priv->eint_irq);
	disable_irq(priv->accdet_irq);
	cancel_delayed_work_sync(&priv->micbias_work);
}

/*
 * Not a wake source: mt6358-irq leaves enabled sub-IRQs able to wake deep
 * sleep, so both IRQs are masked over suspend. A plug change during sleep
 * is level-pending and is handled once the IRQs are enabled again.
 */
static int mt6358_accdet_suspend(struct device *dev)
{
	struct mt6358_accdet *priv = dev_get_drvdata(dev);

	disable_irq(priv->eint_irq);
	disable_irq(priv->accdet_irq);
	return 0;
}

/*
 * If a deep sleep lost the ACCDET setup, set it up again (plug state
 * starts over from "unplugged"; a plugged jack then raises EINT0 again).
 */
static int mt6358_accdet_resume(struct device *dev)
{
	struct mt6358_accdet *priv = dev_get_drvdata(dev);
	unsigned int val;

	regmap_read(priv->regmap, ACCDET_CON01, &val);
	if (!(val & ACCDET_EINT0_EN)) {
		dev_info(dev, "ACCDET setup lost in sleep, re-init\n");
		mutex_lock(&priv->lock);
		cancel_delayed_work(&priv->micbias_work);
		mt6358_accdet_init_hw(priv);
		mt6358_accdet_report(priv);
		mutex_unlock(&priv->lock);
	}

	enable_irq(priv->accdet_irq);
	enable_irq(priv->eint_irq);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(mt6358_accdet_pm, mt6358_accdet_suspend,
				mt6358_accdet_resume);

static const struct of_device_id mt6358_accdet_of_match[] = {
	{ .compatible = "mediatek,mt6358-accdet" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6358_accdet_of_match);

static struct platform_driver mt6358_accdet_driver = {
	.driver = {
		.name = "mt6358-accdet",
		.of_match_table = mt6358_accdet_of_match,
		.pm = pm_sleep_ptr(&mt6358_accdet_pm),
	},
	.probe = mt6358_accdet_probe,
	.remove = mt6358_accdet_remove,
};
module_platform_driver(mt6358_accdet_driver);

MODULE_DESCRIPTION("MediaTek MT6358 ACCDET headset plug detection");
MODULE_LICENSE("GPL");
