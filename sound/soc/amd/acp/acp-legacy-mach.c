// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
//

/*
 * Machine Driver Legacy Support for ACP HW block
 */

#include <sound/core.h>
#include <sound/pcm_params.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include <linux/module.h>

#include "acp-mach.h"

static struct acp_card_drvdata rt5682_rt1019_data = {
	.hs_cpu_id = I2S_SP,
	.amp_cpu_id = I2S_SP,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = RT5682,
	.amp_codec_id = RT1019,
	.dmic_codec_id = DMIC,
	.gpio_spkr_en = EN_SPKR_GPIO_GB,
};

static struct acp_card_drvdata rt5682s_max_data = {
	.hs_cpu_id = I2S_SP,
	.amp_cpu_id = I2S_SP,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = RT5682S,
	.amp_codec_id = MAX98360A,
	.dmic_codec_id = DMIC,
	.gpio_spkr_en = EN_SPKR_GPIO_NONE,
};

static struct acp_card_drvdata rt5682s_rt1019_data = {
	.hs_cpu_id = I2S_SP,
	.amp_cpu_id = I2S_SP,
	.dmic_cpu_id = DMIC,
	.hs_codec_id = RT5682S,
	.amp_codec_id = RT1019,
	.dmic_codec_id = DMIC,
	.gpio_spkr_en = EN_SPKR_GPIO_NONE,
};

static const struct snd_kcontrol_new acp_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Spk"),
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),

};

static const struct snd_soc_dapm_widget acp_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_SPK("Spk", event_spkr_handler),
	SND_SOC_DAPM_SPK("Left Spk", event_spkr_handler),
	SND_SOC_DAPM_SPK("Right Spk", event_spkr_handler),
};

static int acp_asoc_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = NULL;
	struct device *dev = &pdev->dev;
	unsigned int spkr_gpio;
	int ret;

	if (!pdev->id_entry)
		return -EINVAL;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->dev = dev;
	card->owner = THIS_MODULE;
	card->name = pdev->id_entry->name;
	card->dapm_widgets = acp_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(acp_widgets);
	card->controls = acp_controls;
	card->num_controls = ARRAY_SIZE(acp_controls);
	card->drvdata = (struct acp_card_drvdata *)pdev->id_entry->driver_data;
	spkr_gpio = ((struct acp_card_drvdata *)(card->drvdata))->gpio_spkr_en;

	acp_legacy_dai_links_create(card);

	if (gpio_is_valid(spkr_gpio)) {
		ret = devm_gpio_request(dev, spkr_gpio, "spkren");
		if (ret) {
			dev_err(dev, "(%s) gpio request failed: %d\n",
				__func__, ret);
			return ret;
		}
		gpio_direction_output(spkr_gpio, 0);
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev,
				"devm_snd_soc_register_card(%s) failed: %d\n",
				card->name, ret);
		return ret;
	}

	return 0;
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "acp3xalc56821019",
		.driver_data = (kernel_ulong_t)&rt5682_rt1019_data,
	},
	{
		.name = "acp3xalc5682sm98360",
		.driver_data = (kernel_ulong_t)&rt5682s_max_data,
	},
	{
		.name = "acp3xalc5682s1019",
		.driver_data = (kernel_ulong_t)&rt5682s_rt1019_data,
	},
	{ }
};
static struct platform_driver acp_asoc_audio = {
	.driver = {
		.name = "acp_mach",
	},
	.probe = acp_asoc_probe,
	.id_table = board_ids,
};

module_platform_driver(acp_asoc_audio);

MODULE_IMPORT_NS(SND_SOC_AMD_MACH);
MODULE_DESCRIPTION("ACP chrome audio support");
MODULE_ALIAS("platform:acp3xalc56821019");
MODULE_ALIAS("platform:acp3xalc5682sm98360");
MODULE_ALIAS("platform:acp3xalc5682s1019");
MODULE_LICENSE("GPL v2");
