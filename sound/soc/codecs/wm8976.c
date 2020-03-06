/*
 * wm8976.c  --  WM8976 ALSA Soc Audio driver
 *
 * Copyright 2007-9 Wolfson Microelectronics PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <asm/io.h>

#include "wm8976.h"

static struct wm8976_platform_data wm8976_platform_data;
#define GPBBASE 0x56000010
#define GPB2 	2
#define GPB3 	3
#define GPB4 	4
#define L3_MODE 	GPB2
#define L3_DATA 	GPB3
#define L3_CLK 		GPB4
#define GPBREGSIZE 	4

/*
 * wm8976 l3 op code
 */
static void sendbyte(struct l3_pins *adap, unsigned int byte)
{
	int i;

	for (i = 7; i >= 0; i--) {
		adap->setclk(0);	
		adap->setdat(byte>>i & 1);		
		ndelay(adap->data_setup);
		adap->setclk(1);
		ndelay(adap->clock_high);
	}
}

static int wm8976_l3_write(struct l3_pins *adap, u8 addr, u8 *data, int len)
{
	int i;

	adap->setclk(1);
	adap->setdat(1);
	adap->setmode(1);

	for(i=0; i<len; i++)
	{
		sendbyte(adap, data[i]);
	}

	adap->setmode(0);
	ndelay(adap->mode - adap->data_hold - adap->mode_setup);
    adap->setmode(1);
    adap->setdat(1);
	adap->setclk(1);

	return len;
}


/*
 * wm8976 register cache
 * We can't read the WM8976 register space when we are
 * using 2 wire for device control, so we cache them instead.
 */
static const u16 wm8976_reg[WM8976_CACHEREGNUM] = {
	0x0000,/* WM8976_RESET */ 0x0000, 0x0000, 0x0000,
	0x0050,/* WM8976_IFACE */ 0x0000, 0x0000,/* 0x140 WM8976_CLOCK */ 0x0000,
	0x0000, 0x0000, 0x0000, 0x00ff,
	0x00ff, 0x0000, 0x0100, 0x00ff,
	0x00ff, 0x0000, 0x012c, 0x002c,
	0x002c, 0x002c, 0x002c, 0x0000,
	0x0032, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0038, 0x000b, 0x0032, 0x0000,
	0x0008, 0x000c, 0x0093, 0x00e9,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0033, 0x0010, 0x0010, 0x0100,
	0x0100, 0x0002, 0x0001, 0x0001,
	0x0039, 0x0039, 0x0039, 0x0039,
	0x0001, 0x0001,
};

/*
 * read wm8976 register cache
 */
static inline unsigned int wm8976_read_reg_cache(struct snd_soc_codec  *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg == WM8976_RESET)
		return 0;
	if (reg >= WM8976_CACHEREGNUM)
		return -1;
	return cache[reg];
}

/*
 * write wm8976 register cache
 */
static inline void wm8976_write_reg_cache(struct snd_soc_codec  *codec,
	u16 reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg >= WM8976_CACHEREGNUM)
		return;
	cache[reg] = value;
}

/*
 * write to the WM8976 register space
 */
static int wm8976_write(struct snd_soc_codec  *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[2];

	/* data is
	 *   D15..D9 WM8976 register offset
	 *   D8...D0 register data
	 */
	data[0] = (reg << 1) | ((value >> 8) & 0x0001);
	data[1] = value & 0x00ff;

	if(WM8976_HPVOLL == reg || WM8976_HPVOLR == reg)
	{
		data[0]	|= 1;	
	}

	wm8976_write_reg_cache (codec, reg, value);

	if (codec->hw_write(codec->control_data, data, 2) == 2)
		return 0;
	else
		return -1;
}

/*
 * wm8976 codec hw_write
 */
int wm8976_hw_write(void *control, const char *data, int num)
{
	int iRet;
	u8 *udata;
	struct wm8976_platform_data *pd;
	if(num < 0)
	{
		printk(KERN_ERR "invalid hw num.\n");
		return -EINVAL;
	}
	printk(KERN_ERR "%s: reg:%#x, val:%#x\n", __func__, data[0]>>1, (data[0]&1)<<8|data[1]);
	udata = (u8 *)kzalloc(num, GFP_KERNEL);
	if(!udata)
	{
		printk(KERN_ERR "wm8976 kzalloc failed.\n");
		return -ENOMEM;
	}
	memcpy(udata, data, num*sizeof(char));

	pd = (struct wm8976_platform_data *)control;

	iRet = wm8976_l3_write(&pd->l3, 0, udata, num);
	if(num != iRet)
	{
		kfree(udata);
		printk(KERN_ERR "wm8976 hw write failed.\n");
		iRet = -EBUSY;
	}

	kfree(udata);
	return iRet;
}

#define wm8976_reset(c)	wm8976_write(c, WM8976_RESET, 0)

static const char *wm8976_companding[] = {"Off", "NC", "u-law", "A-law" };
static const char *wm8976_deemp[] = {"None", "32kHz", "44.1kHz", "48kHz" };
static const char *wm8976_eqmode[] = {"Capture", "Playback" };
static const char *wm8976_bw[] = {"Narrow", "Wide" };
static const char *wm8976_eq1[] = {"80Hz", "105Hz", "135Hz", "175Hz" };
static const char *wm8976_eq2[] = {"230Hz", "300Hz", "385Hz", "500Hz" };
static const char *wm8976_eq3[] = {"650Hz", "850Hz", "1.1kHz", "1.4kHz" };
static const char *wm8976_eq4[] = {"1.8kHz", "2.4kHz", "3.2kHz", "4.1kHz" };
static const char *wm8976_eq5[] = {"5.3kHz", "6.9kHz", "9kHz", "11.7kHz" };
static const char *wm8976_alc[] =
    {"ALC both on", "ALC left only", "ALC right only", "Limiter" };

static const struct soc_enum wm8976_enum[] = {
	SOC_ENUM_SINGLE(WM8976_COMP, 1, 4, wm8976_companding), /* adc */
	SOC_ENUM_SINGLE(WM8976_COMP, 3, 4, wm8976_companding), /* dac */
	SOC_ENUM_SINGLE(WM8976_DAC,  4, 4, wm8976_deemp),
	SOC_ENUM_SINGLE(WM8976_EQ1,  8, 2, wm8976_eqmode),

	SOC_ENUM_SINGLE(WM8976_EQ1,  5, 4, wm8976_eq1),
	SOC_ENUM_SINGLE(WM8976_EQ2,  8, 2, wm8976_bw),
	SOC_ENUM_SINGLE(WM8976_EQ2,  5, 4, wm8976_eq2),
	SOC_ENUM_SINGLE(WM8976_EQ3,  8, 2, wm8976_bw),

	SOC_ENUM_SINGLE(WM8976_EQ3,  5, 4, wm8976_eq3),
	SOC_ENUM_SINGLE(WM8976_EQ4,  8, 2, wm8976_bw),
	SOC_ENUM_SINGLE(WM8976_EQ4,  5, 4, wm8976_eq4),
	SOC_ENUM_SINGLE(WM8976_EQ5,  8, 2, wm8976_bw),

	SOC_ENUM_SINGLE(WM8976_EQ5,  5, 4, wm8976_eq5),
	SOC_ENUM_SINGLE(WM8976_ALC3,  8, 2, wm8976_alc),
};

static const struct snd_kcontrol_new wm8976_snd_controls[] = {
SOC_SINGLE("Digital Loopback Switch", WM8976_COMP, 0, 1, 0),

SOC_ENUM("ADC Companding", wm8976_enum[0]),
SOC_ENUM("DAC Companding", wm8976_enum[1]),

SOC_SINGLE("Jack Detection Enable", WM8976_JACK1, 6, 1, 0),

SOC_DOUBLE("DAC Inversion Switch", WM8976_DAC, 0, 1, 1, 0),

//SOC_DOUBLE_R("Headphone Playback Volume", WM8976_DACVOLL, WM8976_DACVOLR, 0, 127, 0),

//SOC_SINGLE("High Pass Filter Switch", WM8976_ADC, 8, 1, 0),
SOC_SINGLE("High Pass Filter Switch", WM8976_ADC, 8, 1, 0),
SOC_SINGLE("High Pass Cut Off", WM8976_ADC, 4, 7, 0),

SOC_DOUBLE("ADC Inversion Switch", WM8976_ADC, 0, 1, 1, 0),

SOC_SINGLE("Capture Volume", WM8976_ADCVOL,  0, 127, 0),

SOC_ENUM("Equaliser Function", wm8976_enum[3]),
SOC_ENUM("EQ1 Cut Off", wm8976_enum[4]),
SOC_SINGLE("EQ1 Volume", WM8976_EQ1,  0, 31, 1),

SOC_ENUM("Equaliser EQ2 Bandwith", wm8976_enum[5]),
SOC_ENUM("EQ2 Cut Off", wm8976_enum[6]),
SOC_SINGLE("EQ2 Volume", WM8976_EQ2,  0, 31, 1),

SOC_ENUM("Equaliser EQ3 Bandwith", wm8976_enum[7]),
SOC_ENUM("EQ3 Cut Off", wm8976_enum[8]),
SOC_SINGLE("EQ3 Volume", WM8976_EQ3,  0, 31, 1),

SOC_ENUM("Equaliser EQ4 Bandwith", wm8976_enum[9]),
SOC_ENUM("EQ4 Cut Off", wm8976_enum[10]),
SOC_SINGLE("EQ4 Volume", WM8976_EQ4,  0, 31, 1),

SOC_ENUM("Equaliser EQ5 Bandwith", wm8976_enum[11]),
SOC_ENUM("EQ5 Cut Off", wm8976_enum[12]),
SOC_SINGLE("EQ5 Volume", WM8976_EQ5,  0, 31, 1),

SOC_SINGLE("DAC Playback Limiter Switch", WM8976_DACLIM1,  8, 1, 0),
SOC_SINGLE("DAC Playback Limiter Decay", WM8976_DACLIM1,  4, 15, 0),
SOC_SINGLE("DAC Playback Limiter Attack", WM8976_DACLIM1,  0, 15, 0),

SOC_SINGLE("DAC Playback Limiter Threshold", WM8976_DACLIM2,  4, 7, 0),
SOC_SINGLE("DAC Playback Limiter Boost", WM8976_DACLIM2,  0, 15, 0),

SOC_SINGLE("ALC Enable Switch", WM8976_ALC1,  8, 1, 0),
SOC_SINGLE("ALC Capture Max Gain", WM8976_ALC1,  3, 7, 0),
SOC_SINGLE("ALC Capture Min Gain", WM8976_ALC1,  0, 7, 0),

SOC_SINGLE("ALC Capture ZC Switch", WM8976_ALC2,  8, 1, 0),
SOC_SINGLE("ALC Capture Hold", WM8976_ALC2,  4, 7, 0),
SOC_SINGLE("ALC Capture Target", WM8976_ALC2,  0, 15, 0),

SOC_ENUM("ALC Capture Mode", wm8976_enum[13]),
SOC_SINGLE("ALC Capture Decay", WM8976_ALC3,  4, 15, 0),
SOC_SINGLE("ALC Capture Attack", WM8976_ALC3,  0, 15, 0),

SOC_SINGLE("ALC Capture Noise Gate Switch", WM8976_NGATE,  3, 1, 0),
SOC_SINGLE("ALC Capture Noise Gate Threshold", WM8976_NGATE,  0, 7, 0),

SOC_SINGLE("Capture PGA ZC Switch", WM8976_INPPGA,  7, 1, 0),
SOC_SINGLE("Capture PGA Volume", WM8976_INPPGA,  0, 63, 0),

SOC_DOUBLE_R("Headphone Playback ZC Switch", WM8976_HPVOLL,  WM8976_HPVOLR, 7, 1, 0),
SOC_DOUBLE_R("Headphone Playback Switch", WM8976_HPVOLL,  WM8976_HPVOLR, 6, 1, 1),
SOC_DOUBLE_R("Headphone Playback Volume", WM8976_HPVOLL,  WM8976_HPVOLR, 0, 63, 0),

SOC_DOUBLE_R("Speaker Playback ZC Switch", WM8976_SPKVOLL,  WM8976_SPKVOLR, 7, 1, 0),
SOC_DOUBLE_R("Speaker Playback Switch", WM8976_SPKVOLL,  WM8976_SPKVOLR, 6, 1, 1),
SOC_DOUBLE_R("Speaker Playback Volume", WM8976_SPKVOLL,  WM8976_SPKVOLR, 0, 63, 0),

SOC_SINGLE("Capture Boost(+20dB)", WM8976_ADCBOOST, 8, 1, 0),
};

/* Left Output Mixer */
static const struct snd_kcontrol_new wm8976_left_mixer_controls[] = {
SOC_DAPM_SINGLE("Right PCM Playback Switch", WM8976_OUTPUT, 6, 1, 1),
SOC_DAPM_SINGLE("Left PCM Playback Switch", WM8976_MIXL, 0, 1, 1),
SOC_DAPM_SINGLE("Line Bypass Switch", WM8976_MIXL, 1, 1, 0),
SOC_DAPM_SINGLE("Aux Playback Switch", WM8976_MIXL, 5, 1, 0),
};

/* Right Output Mixer */
static const struct snd_kcontrol_new wm8976_right_mixer_controls[] = {
SOC_DAPM_SINGLE("Left PCM Playback Switch", WM8976_OUTPUT, 5, 1, 1),
SOC_DAPM_SINGLE("Right PCM Playback Switch", WM8976_MIXR, 0, 1, 1),
SOC_DAPM_SINGLE("Line Bypass Switch", WM8976_MIXR, 1, 1, 0),
SOC_DAPM_SINGLE("Aux Playback Switch", WM8976_MIXR, 5, 1, 0),
};

/* Left AUX Input boost vol */
static const struct snd_kcontrol_new wm8976_laux_boost_controls =
SOC_DAPM_SINGLE("Aux Volume", WM8976_ADCBOOST, 0, 3, 0);

/* Left Input boost vol */
static const struct snd_kcontrol_new wm8976_lmic_boost_controls =
SOC_DAPM_SINGLE("Input Volume", WM8976_ADCBOOST, 4, 3, 0);

/* Left Aux In to PGA */
static const struct snd_kcontrol_new wm8976_laux_capture_boost_controls =
SOC_DAPM_SINGLE("Capture Switch", WM8976_ADCBOOST,  8, 1, 0);

/* Left Input P In to PGA */
static const struct snd_kcontrol_new wm8976_lmicp_capture_boost_controls =
SOC_DAPM_SINGLE("Input P Capture Boost Switch", WM8976_INPUT,  0, 1, 0);

/* Left Input N In to PGA */
static const struct snd_kcontrol_new wm8976_lmicn_capture_boost_controls =
SOC_DAPM_SINGLE("Input N Capture Boost Switch", WM8976_INPUT,  1, 1, 0);

// TODO Widgets
static const struct snd_soc_dapm_widget wm8976_dapm_widgets[] = {
#if 0
//SND_SOC_DAPM_MUTE("Mono Mute", WM8976_MONOMIX, 6, 0),
//SND_SOC_DAPM_MUTE("Speaker Mute", WM8976_SPKMIX, 6, 0),

SND_SOC_DAPM_MIXER("Speaker Mixer", WM8976_POWER3, 2, 0,
	&wm8976_speaker_mixer_controls[0],
	ARRAY_SIZE(wm8976_speaker_mixer_controls)),
SND_SOC_DAPM_MIXER("Mono Mixer", WM8976_POWER3, 3, 0,
	&wm8976_mono_mixer_controls[0],
	ARRAY_SIZE(wm8976_mono_mixer_controls)),
SND_SOC_DAPM_DAC("DAC", "HiFi Playback", WM8976_POWER3, 0, 0),
SND_SOC_DAPM_ADC("ADC", "HiFi Capture", WM8976_POWER3, 0, 0),
SND_SOC_DAPM_PGA("Aux Input", WM8976_POWER1, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("SpkN Out", WM8976_POWER3, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("SpkP Out", WM8976_POWER3, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mono Out", WM8976_POWER3, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mic PGA", WM8976_POWER2, 2, 0, NULL, 0),

SND_SOC_DAPM_PGA("Aux Boost", SND_SOC_NOPM, 0, 0,
	&wm8976_aux_boost_controls, 1),
SND_SOC_DAPM_PGA("Mic Boost", SND_SOC_NOPM, 0, 0,
	&wm8976_mic_boost_controls, 1),
SND_SOC_DAPM_SWITCH("Capture Boost", SND_SOC_NOPM, 0, 0,
	&wm8976_capture_boost_controls),

SND_SOC_DAPM_MIXER("Boost Mixer", WM8976_POWER2, 4, 0, NULL, 0),

SND_SOC_DAPM_MICBIAS("Mic Bias", WM8976_POWER1, 4, 0),

SND_SOC_DAPM_INPUT("MICN"),
SND_SOC_DAPM_INPUT("MICP"),
SND_SOC_DAPM_INPUT("AUX"),
SND_SOC_DAPM_OUTPUT("MONOOUT"),
SND_SOC_DAPM_OUTPUT("SPKOUTP"),
SND_SOC_DAPM_OUTPUT("SPKOUTN"),
#endif
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* Mono output mixer */
	{"Mono Mixer", "PCM Playback Switch", "DAC"},
	{"Mono Mixer", "Aux Playback Switch", "Aux Input"},
	{"Mono Mixer", "Line Bypass Switch", "Boost Mixer"},

	/* Speaker output mixer */
	{"Speaker Mixer", "PCM Playback Switch", "DAC"},
	{"Speaker Mixer", "Aux Playback Switch", "Aux Input"},
	{"Speaker Mixer", "Line Bypass Switch", "Boost Mixer"},

	/* Outputs */
	{"Mono Out", NULL, "Mono Mixer"},
	{"MONOOUT", NULL, "Mono Out"},
	{"SpkN Out", NULL, "Speaker Mixer"},
	{"SpkP Out", NULL, "Speaker Mixer"},
	{"SPKOUTN", NULL, "SpkN Out"},
	{"SPKOUTP", NULL, "SpkP Out"},

	/* Boost Mixer */
	{"Boost Mixer", NULL, "ADC"},
	{"Capture Boost Switch", "Aux Capture Boost Switch", "AUX"},
	{"Aux Boost", "Aux Volume", "Boost Mixer"},
	{"Capture Boost", "Capture Switch", "Boost Mixer"},
	{"Mic Boost", "Mic Volume", "Boost Mixer"},

	/* Inputs */
	{"MICP", NULL, "Mic Boost"},
	{"MICN", NULL, "Mic PGA"},
	{"Mic PGA", NULL, "Capture Boost"},
	{"AUX", NULL, "Aux Input"},
};

static int wm8976_add_widgets(struct snd_soc_codec *codec) __attribute__((unused));

static int wm8976_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(&codec->dapm, wm8976_dapm_widgets,
				  ARRAY_SIZE(wm8976_dapm_widgets));

	snd_soc_dapm_add_routes(&codec->dapm, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_new_widgets(&codec->dapm);
	return 0;
}

struct _pll_div {
	unsigned int pre:4; /* prescale - 1 */
	unsigned int n:4;
	unsigned int k;
};

static struct _pll_div pll_div;

/* The size in bits of the pll divide multiplied by 10
 * to allow rounding later */
#define FIXED_PLL_SIZE ((1 << 24) * 10)

static void pll_factors(unsigned int target, unsigned int source)
{
	unsigned long long Kpart;
	unsigned int K, Ndiv, Nmod;

	Ndiv = target / source;
	if (Ndiv < 6) {
		source >>= 1;
		pll_div.pre = 1;
		Ndiv = target / source;
	} else
		pll_div.pre = 0;

	if ((Ndiv < 6) || (Ndiv > 12))
		printk(KERN_WARNING
			"WM8976 N value outwith recommended range! N = %d\n",Ndiv);

	pll_div.n = Ndiv;
	Nmod = target % source;
	Kpart = FIXED_PLL_SIZE * (long long)Nmod;

	do_div(Kpart, source);

	K = Kpart & 0xFFFFFFFF;

	/* Check if we need to round */
	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	K /= 10;

	pll_div.k = K;
}

static int wm8976_set_dai_pll(struct snd_soc_dai *codec_dai,
		int pll_id, int source, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	if(freq_in == 0 || freq_out == 0) {
		reg = wm8976_read_reg_cache(codec, WM8976_POWER1);
		wm8976_write(codec, WM8976_POWER1, reg & 0x1df);
		return 0;
	}

	pll_factors(freq_out * 8, freq_in);

	wm8976_write(codec, WM8976_PLLN, (pll_div.pre << 4) | pll_div.n);
	wm8976_write(codec, WM8976_PLLK1, pll_div.k >> 18);
	wm8976_write(codec, WM8976_PLLK1, (pll_div.k >> 9) && 0x1ff);
	wm8976_write(codec, WM8976_PLLK1, pll_div.k && 0x1ff);
	reg = wm8976_read_reg_cache(codec, WM8976_POWER1);
	wm8976_write(codec, WM8976_POWER1, reg | 0x020);
	
	
	return 0;
}

static int wm8976_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = wm8976_read_reg_cache(codec, WM8976_IFACE) & 0x3;
	u16 clk = wm8976_read_reg_cache(codec, WM8976_CLOCK) & 0xfffe;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		clk |= 0x0001;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0010;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0008;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x00018;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x0180;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0100;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x0080;
		break;
	default:
		return -EINVAL;
	}

	wm8976_write(codec, WM8976_IFACE, iface);
	wm8976_write(codec, WM8976_CLOCK, clk);

	return 0;
}

static int wm8976_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	u16 iface = wm8976_read_reg_cache(codec, WM8976_IFACE) & 0xff9f;
	u16 adn = wm8976_read_reg_cache(codec, WM8976_ADD) & 0x1f1;

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x0020;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x0040;
		break;
	}

	/* filter coefficient */
	switch (params_rate(params)) {
	case SNDRV_PCM_RATE_8000:
		adn |= 0x5 << 1;
		break;
	case SNDRV_PCM_RATE_11025:
		adn |= 0x4 << 1;
		break;
	case SNDRV_PCM_RATE_16000:
		adn |= 0x3 << 1;
		break;
	case SNDRV_PCM_RATE_22050:
		adn |= 0x2 << 1;
		break;
	case SNDRV_PCM_RATE_32000:
		adn |= 0x1 << 1;
		break;
	}

	/* set iface */
	wm8976_write(codec, WM8976_IFACE, iface);
	wm8976_write(codec, WM8976_ADD, adn);

	return 0;
}

static int wm8976_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	switch (div_id) {
	case WM8976_MCLKDIV:
		reg = wm8976_read_reg_cache(codec, WM8976_CLOCK) & 0x11f;
		wm8976_write(codec, WM8976_CLOCK, reg | div);
		break;
	case WM8976_BCLKDIV:
		reg = wm8976_read_reg_cache(codec, WM8976_CLOCK) & 0x1c7;
		wm8976_write(codec, WM8976_CLOCK, reg | div);
		break;
	case WM8976_OPCLKDIV:
		reg = wm8976_read_reg_cache(codec, WM8976_GPIO) & 0x1cf;
		wm8976_write(codec, WM8976_GPIO, reg | div);
		break;
	case WM8976_DACOSR:
		reg = wm8976_read_reg_cache(codec, WM8976_DAC) & 0x1f7;
		wm8976_write(codec, WM8976_DAC, reg | div);
		break;
	case WM8976_ADCOSR:
		reg = wm8976_read_reg_cache(codec, WM8976_ADC) & 0x1f7;
		wm8976_write(codec, WM8976_ADC, reg | div);
		break;
	case WM8976_MCLKSEL:
		reg = wm8976_read_reg_cache(codec, WM8976_CLOCK) & 0x0ff;
		wm8976_write(codec, WM8976_CLOCK, reg | div);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8976_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = wm8976_read_reg_cache(codec, WM8976_DAC) & 0xffbf;

	if(mute)
		wm8976_write(codec, WM8976_DAC, mute_reg | 0x40);
	else
		wm8976_write(codec, WM8976_DAC, mute_reg);

	return 0;
}

/*
 * wm8976 JACK DETECT
 */
static int wm8976_set_jack_detect(struct snd_soc_codec *codec, int enable)
{
	unsigned int reg;
	if(!codec)
	{
		printk(KERN_ERR "%s: dai ptr is err.\n", __func__);
		return -EINVAL;
	}
	
	if(enable)
	{
		/* enable jack detect, gpio2 is use to do jack detect */
		wm8976_write(codec, WM8976_JACK1, 0x50);
		/* enable jack for headset and speaker */
		wm8976_write(codec, WM8976_JACK2, 0x21);
		/* enable slow clock for debounce circuitry */
		wm8976_write(codec, WM8976_ADD, 0x01);
	}
	else
	{
		reg = wm8976_read_reg_cache(codec, WM8976_JACK1);
		reg &= ~(0x50);
		wm8976_write(codec, WM8976_JACK1, reg);

		reg = wm8976_read_reg_cache(codec, WM8976_JACK2);
		reg &= ~(0x21);
		wm8976_write(codec, WM8976_JACK2, reg);

		reg = wm8976_read_reg_cache(codec, WM8976_ADD);
		reg |= ~(0x01);
		wm8976_write(codec, WM8976_ADD, reg);
	}

	return 0;
}

/*
 * wm8976 beep(speaker) init
 */
static int wm8976_beep_init(struct snd_soc_codec *codec)
{
	if(!codec)
	{
		printk(KERN_ERR "%s: codec ptr is err.\n", __func__);
		return -EINVAL;		
	}
	/* enable speaker */
	//wm8976_write(codec, WM8976_BEEP, 0x10);	
	return 0;
}

/* TODO: liam need to make this lower power with dapm */
static int wm8976_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{

	switch (level) {
	case SND_SOC_BIAS_ON:
//		wm8976_write(codec, WM8976_POWER1, 0x1ff);
//		wm8976_write(codec, WM8976_POWER2, 0x1ff);
//		wm8976_write(codec, WM8976_POWER3, 0x1ff);
		wm8976_write(codec, WM8976_POWER1, 0xDF);
		wm8976_write(codec, WM8976_POWER2, 0x185);
		wm8976_write(codec, WM8976_POWER3, 0xFF);
		break;
	case SND_SOC_BIAS_STANDBY:
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_OFF:
		wm8976_write(codec, WM8976_POWER1, 0x0);
		wm8976_write(codec, WM8976_POWER2, 0x0);
		wm8976_write(codec, WM8976_POWER3, 0x0);
		break;
	}

	return 0;
}

#define WM8976_RATES \
	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 | \
	SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000)

#define WM8976_FORMATS \
	(SNDRV_PCM_FORMAT_S16_LE | SNDRV_PCM_FORMAT_S20_3LE | \
	SNDRV_PCM_FORMAT_S24_3LE | SNDRV_PCM_FORMAT_S24_LE)

static struct snd_soc_dai_ops wm8976_dai_ops = {
	.hw_params = wm8976_hw_params,
	.digital_mute = wm8976_mute,
	.set_fmt = wm8976_set_dai_fmt,
	.set_clkdiv = wm8976_set_dai_clkdiv,
	.set_pll = wm8976_set_dai_pll,
};

static int snd_soc_wm8976_probe(struct snd_soc_codec *codec)
{
	int ret = 0;

	if(NULL == codec)
	{
		ret = -EINVAL;
		printk(KERN_ERR "input codec ptr is null\n");
		goto pcm_err;	
	}
	
	wm8976_platform_data.gpbcon = ioremap(GPBBASE, GPBREGSIZE);
	wm8976_platform_data.gpbdat = ioremap(GPBBASE+GPBREGSIZE, GPBREGSIZE);

	/* GPB 4: L3CLOCK */
	/* GPB 3: L3DATA */
	/* GPB 2: L3MODE */
    *wm8976_platform_data.gpbcon &= ~((3<<4) | (3<<6) | (3<<8));
    *wm8976_platform_data.gpbcon |= ((1<<4) | (1<<6) | (1<<8));

	if(!codec->control_data)
	{
		codec->control_data = (void *)&wm8976_platform_data;
		codec->control_type = 0; //represent wm8976 self l3 mode
	}

	codec->reg_cache = (void *)wm8976_reg;
	codec->reg_size = WM8976_CACHEREGNUM;
	codec->cache_init = 1;

	codec->hw_write = wm8976_hw_write;

	wm8976_reset(codec);
	wm8976_set_bias_level(codec, SND_SOC_BIAS_ON);
	wm8976_set_jack_detect(codec, 1);
	wm8976_beep_init(codec);

	ret = snd_soc_add_codec_controls(codec, wm8976_snd_controls,
				 ARRAY_SIZE(wm8976_snd_controls));
	//wm8976_add_widgets(codec);

	return ret;

pcm_err:
	return ret;
}

static int snd_soc_wm8976_remove(struct snd_soc_codec *codec)
{
	iounmap(wm8976_platform_data.gpbcon);
	iounmap(wm8976_platform_data.gpbdat);
	return 0;
}

static int snd_soc_wm8976_suspend(struct snd_soc_codec *codec)
{
	wm8976_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int snd_soc_wm8976_resume(struct snd_soc_codec *codec)
{
	int i;
	u8 data[2];
	u16 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(wm8976_reg); i++) {
		data[0] = (i << 1) | ((cache[i] >> 8) & 0x0001);
		data[1] = cache[i] & 0x00ff;
		wm8976_write(codec, data[0], data[1]);
		//codec->hw_write(codec->control_data, data, 2);
	}
	wm8976_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	wm8976_set_bias_level(codec, SND_SOC_BIAS_ON);
	return 0;
}

static void wm8976_l3_setdat(int data)
{
	if(data < 0)
	{
		printk(KERN_ERR "invaild data.\n");
		return;
	}

	if(data)
	{
		*wm8976_platform_data.gpbdat |= 1 << L3_DATA; 
	}
	else
	{
		*wm8976_platform_data.gpbdat &= ~(1 << L3_DATA);
	}

}

static void wm8976_l3_setclk(int clk)
{
	if(clk < 0)
	{
		printk(KERN_ERR "invaild clk.\n");
		return;
	}

	if(clk)
	{
		*wm8976_platform_data.gpbdat |= 1 << L3_CLK; 
	}
	else
	{
		*wm8976_platform_data.gpbdat &= ~(1 << L3_CLK);
	}
}
static void wm8976_l3_setmode(int mode)
{
	if(mode < 0)
	{
		printk(KERN_ERR "invaild mode.\n");
		return;
	}

	if(mode)
	{
		*wm8976_platform_data.gpbdat |= 1 << L3_MODE; 
	}
	else
	{
		*wm8976_platform_data.gpbdat &= ~(1 << L3_MODE);
	}	
}

static struct snd_soc_codec_driver wm8976_codec_driver = {
	.probe = snd_soc_wm8976_probe,
	.remove = snd_soc_wm8976_remove,
	.suspend = snd_soc_wm8976_suspend,
	.resume = snd_soc_wm8976_resume,
	.reg_cache_size = WM8976_CACHEREGNUM,
	.reg_word_size = sizeof(u16),

//	.controls = wm8976_snd_controls,
//	.num_controls = ARRAY_SIZE(wm8976_snd_controls),
//	.dapm_widgets = wm8976_dapm_widgets,
//	.num_dapm_widgets = ARRAY_SIZE(wm8976_dapm_widgets),
//	.dapm_routes = audio_map,
//	.num_dapm_routes = ARRAY_SIZE(audio_map),

	/* codec IO */
	.read = wm8976_read_reg_cache,
	.write = wm8976_write,
};


static struct wm8976_platform_data wm8976_platform_data = {
	.l3 = {
		.setdat = wm8976_l3_setdat,
		.setclk = wm8976_l3_setclk,
		.setmode = wm8976_l3_setmode,
		.data_hold = 100,    /* tsh = 80ns */
		.data_setup = 100,   /* tsl = 80ns */
		.clock_high = 100,   /* tsh = 80ns */
		.mode_hold = 1600,   /* Tscl = Tscy*8 = 1600ns */
		.mode_setup =   0,    /* Tcsh = 40ns */
		.mode = 1640,         /* mode_hold + mode_setup + mode_unhold(40ns) = 1640ns */
	},
};

static struct snd_soc_dai_driver wm8976_dai_driver[] = {
	{
		.id = 0,
		.name = "wm8976-dai",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = WM8976_RATES,
			.formats = WM8976_FORMATS,},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 1,
			.rates = WM8976_RATES,
			.formats = WM8976_FORMATS,},
		.ops = &wm8976_dai_ops,
	},
};

static void s3c24xx_wm8976_codec_device_release(struct device *dev)
{
}

static int wm8976_platform_probe(struct platform_device *pdev)
{
	snd_soc_register_codec(&pdev->dev, &wm8976_codec_driver, 
			wm8976_dai_driver, ARRAY_SIZE(wm8976_dai_driver));
	return 0;
}

static int wm8976_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_device s3c2440_wm8976_codec_device = {
	.name = "wm8976-codec",
	.id   = -1,
	.dev  = {
		.release = s3c24xx_wm8976_codec_device_release,
	},
};

static struct platform_driver s3c2440_wm8976_codec_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "wm8976-codec",
	},
	
	.probe = wm8976_platform_probe,
	.remove = __devexit_p(wm8976_platform_remove),
};

static int __init wm8976_init(void)
{
	int iRet;

	iRet = platform_device_register(&s3c2440_wm8976_codec_device);
	if(iRet < 0)
	{
		printk(KERN_ERR "register s3c2440_wm8976_codec_device failled\n");
		return -EBUSY;
	}

	iRet = platform_driver_register(&s3c2440_wm8976_codec_driver);
	if(iRet < 0)
	{
		printk(KERN_ERR "register s3c2440_wm8976_codec_driver failled\n");
		return -EBUSY;	
	}

	return 0;
}

static void __exit wm8976_exit(void)
{
	platform_device_unregister(&s3c2440_wm8976_codec_device);
	platform_driver_unregister(&s3c2440_wm8976_codec_driver);
}

module_init(wm8976_init);
module_exit(wm8976_exit);


MODULE_AUTHOR("test, <test@test.org>");
MODULE_DESCRIPTION("ASoC WM8976 driver");
MODULE_LICENSE("GPL");
