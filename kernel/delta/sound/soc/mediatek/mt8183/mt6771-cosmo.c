// SPDX-License-Identifier: GPL-2.0
/*
 * Planet Computers Cosmo Communicator (MT6771) sound card.
 *
 * Based on mt8183-mt6358-ts3a227-max98357.c. The Cosmo has no I2S
 * amplifiers: the MT6358 line-out (LOL) feeds two pulse-mode GPIO speaker
 * amplifiers (aw8738, added as aux devices), and the headphone jack is on
 * the MT6358 headphone outputs. The board routing comes from the device
 * tree ("audio-routing"), the amplifiers from "audio-aux-devs".
 */

#include <linux/module.h>
#include <linux/of.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "mt8183-afe-common.h"

static int cosmo_playback_startup(struct snd_pcm_substream *substream)
{
	static const unsigned int rates[] = { 48000 };
	static const struct snd_pcm_hw_constraint_list constraints_rates = {
		.count = ARRAY_SIZE(rates),
		.list = rates,
	};
	static const unsigned int channels[] = { 2 };
	static const struct snd_pcm_hw_constraint_list constraints_channels = {
		.count = ARRAY_SIZE(channels),
		.list = channels,
	};
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &constraints_rates);
	runtime->hw.channels_max = 2;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &constraints_channels);
	runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE;
	snd_pcm_hw_constraint_msbits(runtime, 0, 16, 16);

	return 0;
}

static const struct snd_soc_ops cosmo_playback_ops = {
	.startup = cosmo_playback_startup,
};

/* FE */
SND_SOC_DAILINK_DEFS(playback1,
	DAILINK_COMP_ARRAY(COMP_CPU("DL1")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback2,
	DAILINK_COMP_ARRAY(COMP_CPU("DL2")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback3,
	DAILINK_COMP_ARRAY(COMP_CPU("DL3")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture1,
	DAILINK_COMP_ARRAY(COMP_CPU("UL1")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture2,
	DAILINK_COMP_ARRAY(COMP_CPU("UL2")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture3,
	DAILINK_COMP_ARRAY(COMP_CPU("UL3")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture_mono,
	DAILINK_COMP_ARRAY(COMP_CPU("UL_MONO_1")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

/* BE */
SND_SOC_DAILINK_DEFS(primary_codec,
	DAILINK_COMP_ARRAY(COMP_CPU("ADDA")),
	DAILINK_COMP_ARRAY(COMP_CODEC("mt6358-sound", "mt6358-snd-codec-aif1")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

#define COSMO_FE(_name, _dir, _link)					\
	{								\
		.name = _name,						\
		.stream_name = _name,					\
		.trigger = { SND_SOC_DPCM_TRIGGER_PRE,			\
			     SND_SOC_DPCM_TRIGGER_PRE },		\
		.dynamic = 1,						\
		._dir = 1,						\
		SND_SOC_DAILINK_REG(_link),				\
	}

static struct snd_soc_dai_link cosmo_dai_links[] = {
	/* FE */
	{
		.name = "Playback_1",
		.stream_name = "Playback_1",
		.trigger = { SND_SOC_DPCM_TRIGGER_PRE,
			     SND_SOC_DPCM_TRIGGER_PRE },
		.dynamic = 1,
		.playback_only = 1,
		.ops = &cosmo_playback_ops,
		SND_SOC_DAILINK_REG(playback1),
	},
	COSMO_FE("Playback_2", playback_only, playback2),
	COSMO_FE("Playback_3", playback_only, playback3),
	COSMO_FE("Capture_1", capture_only, capture1),
	COSMO_FE("Capture_2", capture_only, capture2),
	COSMO_FE("Capture_3", capture_only, capture3),
	COSMO_FE("Capture_Mono_1", capture_only, capture_mono),

	/* BE */
	{
		.name = "Primary Codec",
		.no_pcm = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(primary_codec),
	},
};

static const struct snd_soc_dapm_widget cosmo_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
};

static const struct snd_kcontrol_new cosmo_snd_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
};

static struct snd_soc_card cosmo_card = {
	.name = "cosmo",
	.owner = THIS_MODULE,
	.dai_link = cosmo_dai_links,
	.num_links = ARRAY_SIZE(cosmo_dai_links),
	.dapm_widgets = cosmo_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cosmo_dapm_widgets),
	.controls = cosmo_snd_controls,
	.num_controls = ARRAY_SIZE(cosmo_snd_controls),
	.fully_routed = true,
};

static int cosmo_sound_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &cosmo_card;
	struct device_node *platform_node;
	struct snd_soc_dai_link *dai_link;
	int ret, i;

	platform_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,platform", 0);
	if (!platform_node)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "property 'mediatek,platform' missing\n");

	card->dev = &pdev->dev;

	for_each_card_prelinks(card, i, dai_link) {
		if (!dai_link->platforms->name)
			dai_link->platforms->of_node = platform_node;
	}

	ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
	if (ret)
		goto out;

	ret = snd_soc_of_parse_aux_devs(card, "audio-aux-devs");
	if (ret)
		goto out;

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err_probe(&pdev->dev, ret, "failed to register card\n");
out:
	of_node_put(platform_node);
	return ret;
}

static const struct of_device_id cosmo_sound_dt_match[] = {
	{ .compatible = "planet,cosmo-communicator-sound" },
	{ }
};
MODULE_DEVICE_TABLE(of, cosmo_sound_dt_match);

static struct platform_driver cosmo_sound_driver = {
	.driver = {
		.name = "mt6771-cosmo-sound",
		.of_match_table = cosmo_sound_dt_match,
		.pm = &snd_soc_pm_ops,
	},
	.probe = cosmo_sound_probe,
};
module_platform_driver(cosmo_sound_driver);

MODULE_DESCRIPTION("Cosmo Communicator (MT6771 + MT6358) ALSA SoC machine driver");
MODULE_LICENSE("GPL");
