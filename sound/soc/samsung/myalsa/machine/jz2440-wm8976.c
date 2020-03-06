/*
 * jz2440 for s3c2440 soc.
 * wm8976 is used for jz2440 codec.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/module.h>

#include <sound/soc.h>

#include <plat/regs-iis.h>
#include "../../s3c24xx-i2s.h"

/* #define ENFORCE_RATES 1 */
/*
  Unfortunately the S3C24XX in master mode has a limited capacity of
  generating the clock for the codec. If you define this only rates
  that are really available will be enforced. But be careful, most
  user level application just want the usual sampling frequencies (8,
  11.025, 22.050, 44.1 kHz) and anyway resampling is a costly
  operation for embedded systems. So if you aren't very lucky or your
  hardware engineer wasn't very forward-looking it's better to leave
  this undefined. If you do so an approximate value for the requested
  sampling rate in the range -/+ 5% will be chosen. If this in not
  possible an error will be returned.
*/

#define DEBUG

#define ABS(a, b) ((a>b)?(a-b):(b-a))

static DEFINE_MUTEX(clk_lock);

static unsigned int rates[33 * 2] = {
	8000, 
	11025,
	16000,
	22050,
	32000,
	44100,
	48000,
	64000,
	88200,
	96000,
};

static int s3c2440_wm8976_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int clk = 0;
	int ret = 0;
	int clk_source, fs_mode;
	unsigned long rate = params_rate(params);
	long err, cerr;
	unsigned int div = 0;
	int i, bi = 0;
	struct clk *iis_clk;

	err = 999999;

	for (i = 0; i < 2*33; i++) {
		cerr = rates[i] - rate;
		if (cerr < 0)
			cerr = -cerr;
		if (cerr < err) {
			err = cerr;
			bi = i;
		}
	}
	if (bi / 33 == 1)
		fs_mode = S3C2410_IISMOD_256FS;
	else
		fs_mode = S3C2410_IISMOD_384FS;
	if (bi % 33 == 0) {
		clk_source = S3C24XX_CLKSRC_MPLL;
		pr_err("%s not support it yet.\n", __func__);
		return -ENOTSUPP;
	} else {
		clk_source = S3C24XX_CLKSRC_PCLK;
	}
	pr_debug("%s desired rate %lu, %d\n", __func__, rate, bi);


	clk = (fs_mode == S3C2410_IISMOD_384FS ? 384 : 256) * rate;
	pr_debug("%s will use: %s %s %d sysclk %d err %ld\n", __func__,
		 fs_mode == S3C2410_IISMOD_384FS ? "384FS" : "256FS",
		 clk_source == S3C24XX_CLKSRC_MPLL ? "MPLLin" : "PCLK",
		 div, clk, err);
	
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S | \
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, clk_source , clk,
			SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	iis_clk = clk_get(NULL, "iis");	
	if (IS_ERR(iis_clk)) {
		pr_err("failed to get iis_clock\n");
		return PTR_ERR(iis_clk);
	}
	if(fs_mode == S3C2410_IISMOD_384FS)
	{
		div = clk_get_rate(iis_clk) / (384 * rate);
	}
	else
	{		
		div = 1;
	}	
	clk_put(iis_clk);

	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_MCLK, fs_mode);
	if (ret < 0)
	{
		printk(KERN_ERR "S3C24XX_WM8976: snd_soc_dai_set_clkdiv MCLK\n");
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_BCLK,
			S3C2410_IISMOD_32FS);
	if (ret < 0)
	{
		printk(KERN_ERR "S3C24XX_WM8976: snd_soc_dai_set_clkdiv BLK\n");
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_PRESCALER,
			S3C24XX_PRESCALE(div, div));
	if (ret < 0)
	{
		printk(KERN_ERR "S3C24XX_WM8976: snd_soc_dai_set_clkdiv PRESCALE\n");
		return ret;
	}

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, clk,
			SND_SOC_CLOCK_OUT);
	if (ret < 0)
	{
		printk(KERN_WARNING "S3C24XX_WM8976: codec not suppport it yet(set sysclk).\n");
	}
	return 0;
}

static struct snd_soc_ops s3c2440_wm8976_ops = {
	.startup = NULL,
	.shutdown = NULL,
	.hw_params = s3c2440_wm8976_hw_params,
};

static struct snd_soc_dai_link s3c2440_wm8976_dai_link = {
	.name = "WM8976",
	.stream_name = "WM8976",
	.codec_name = "wm8976-codec",
	.codec_dai_name = "wm8976-dai",
	.cpu_dai_name = "s3c2440-iis",
	.ops = &s3c2440_wm8976_ops,
	.platform_name	= "s3c2440-dma",
};

static struct snd_soc_card snd_soc_s3c2440_wm8976 = {
	.name = "S3C24XX_WM8976",
	.owner = THIS_MODULE,
	.dai_link = &s3c2440_wm8976_dai_link,
	.num_links = 1,
};

static void s3c2440_wm8976_snd_device_release(struct device *dev)
{
}


static struct platform_device s3c2440_wm8976_snd_device = {
	.name = "soc-audio",
	.id   = -1,
	.dev  = {
		.release = s3c2440_wm8976_snd_device_release,
	},
};

static int s3c2440_wm8976_init(void)
{
	int iRet = 0;
	platform_set_drvdata(&s3c2440_wm8976_snd_device,
			     &snd_soc_s3c2440_wm8976);

	iRet = platform_device_register(&s3c2440_wm8976_snd_device);
	if(iRet < 0)
	{
		printk(KERN_ERR "register s3c2440_wm8976_snd_device failled\n");
		return -EBUSY;
	}
	return 0;
}

static void s3c2440_wm8976_exit(void)
{
	platform_device_unregister(&s3c2440_wm8976_snd_device);
}

module_init(s3c2440_wm8976_init);
module_exit(s3c2440_wm8976_exit);

MODULE_AUTHOR("test <test@test.org>");
MODULE_DESCRIPTION("S3C24XX_Wm8976 ALSA SoC audio driver");
MODULE_LICENSE("GPL");

