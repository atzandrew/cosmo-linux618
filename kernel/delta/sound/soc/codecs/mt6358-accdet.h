/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MT6358 ACCDET (headset plug detection), Cosmo Communicator.
 */

#ifndef _MT6358_ACCDET_H_
#define _MT6358_ACCDET_H_

#include <sound/soc.h>
#include <sound/jack.h>

int mt6358_accdet_enable_jack_detect(struct snd_soc_component *component,
				     struct snd_soc_jack *jack);

#endif
