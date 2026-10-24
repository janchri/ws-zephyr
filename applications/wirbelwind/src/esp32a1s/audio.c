/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Audio output of the ESP32-A1S: I2S0 (master) -> AC101 codec (slave).
 *
 * The output always runs at 44.1 kHz stereo, so the codec PLL never has to be
 * reconfigured: other sample rates and mono are converted (resample.c), e.g.
 * audio books at 22.05 kHz mono.
 *
 * The AC101 PLL runs from the I2S master clock. The ESP32 can only output it
 * on GPIO0 through CLK_OUT1, which pinctrl does not cover, so it is routed
 * here.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <soc/io_mux_reg.h>
#include <soc/soc.h>

#include "ac101.h"
#include "audio.h"
#include "resample.h"

LOG_MODULE_DECLARE(ww, CONFIG_WW_LOG_LEVEL);

#define AC101_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(x_powers_ac101)

#define OUT_RATE     44100
#define BLOCK_FRAMES 1024 /* 23 ms */
#define BLOCK_SIZE   (BLOCK_FRAMES * 2 * sizeof(int16_t))
#define NUM_BLOCKS   (CONFIG_I2S_ESP32_TX_BLOCK_COUNT + 2)
/* Blocks queued before the I2S is started */
#define START_BLOCKS 2

/*
 * The DMA needs internal RAM. A plain array lands in .bss; K_MEM_SLAB_DEFINE
 * would put it into the noinit region, which the thread stacks fill up.
 */
static uint8_t __aligned(4) tx_buf[NUM_BLOCKS * BLOCK_SIZE];
static struct k_mem_slab tx_slab;

static const struct device *const i2s_dev = DEVICE_DT_GET(DT_PHANDLE(AC101_NODE, i2s));

/* Block being filled */
static int16_t *blk;
static size_t blk_frames;
static unsigned int queued;
static bool running;

static struct ww_resampler resampler;

static void route_mclk_to_gpio0(void)
{
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
	/* CLK_OUT1 source 0 = I2S0 master clock */
	REG_SET_FIELD(PIN_CTRL, CLK_OUT1, 0);
}

int audio_sink_init(void)
{
	const struct i2s_config cfg = {
		.word_size = 16,
		.channels = 2,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_CONTROLLER | I2S_OPT_FRAME_CLK_CONTROLLER,
		.frame_clk_freq = OUT_RATE,
		.mem_slab = &tx_slab,
		.block_size = BLOCK_SIZE,
		.timeout = 1000,
	};
	int ret;

	if (!device_is_ready(i2s_dev)) {
		LOG_ERR("I2S not ready");
		return -ENODEV;
	}

	ret = k_mem_slab_init(&tx_slab, tx_buf, BLOCK_SIZE, NUM_BLOCKS);
	if (ret) {
		return ret;
	}

	/* Configuring the I2S starts its clocks, the codec PLL locks to MCLK */
	ret = i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
	if (ret) {
		LOG_ERR("I2S configuration failed: %d", ret);
		return ret;
	}
	route_mclk_to_gpio0();

	ret = ac101_init();
	if (ret) {
		return ret;
	}
	ac101_set_output_volume(CONFIG_WW_AC101_HP_VOLUME, CONFIG_WW_AC101_SPK_VOLUME);

	return ac101_enable_output(true);
}

int audio_sink_start(uint32_t rate, uint8_t channels)
{
	ww_resample_init(&resampler, rate, channels, OUT_RATE);

	if (rate != OUT_RATE || channels != 2) {
		LOG_INF("Converting %u Hz/%u ch to %u Hz/2 ch", rate, channels, OUT_RATE);
	}

	return 0;
}

static void reset_i2s(void)
{
	/* DROP frees the queued blocks; after an underrun only PREPARE is allowed */
	if (i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP) != 0) {
		i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
	}
	running = false;
	queued = 0;
}

static int submit_block(void)
{
	int ret = i2s_write(i2s_dev, blk, BLOCK_SIZE);

	if (ret == -EIO) {
		/* The queue ran empty (e.g. slow SD card): restart the stream */
		LOG_WRN("I2S underrun");
		reset_i2s();
		ret = i2s_write(i2s_dev, blk, BLOCK_SIZE);
	}
	if (ret) {
		LOG_ERR("I2S write failed: %d", ret);
		k_mem_slab_free(&tx_slab, blk);
		blk = NULL;
		return ret;
	}
	blk = NULL;
	queued++;

	if (!running && queued >= START_BLOCKS) {
		ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
		running = (ret == 0);
	}

	return ret;
}

static int put_frame(int16_t l, int16_t r)
{
	if (blk == NULL) {
		/* Waits until the I2S has played a block: this paces the player */
		if (k_mem_slab_alloc(&tx_slab, (void **)&blk, K_MSEC(1000)) != 0) {
			LOG_ERR("I2S stalled");
			reset_i2s();
			return -EIO;
		}
		blk_frames = 0;
	}

	blk[2 * blk_frames] = l;
	blk[2 * blk_frames + 1] = r;

	if (++blk_frames == BLOCK_FRAMES) {
		return submit_block();
	}

	return 0;
}

int audio_sink_write(const int16_t *samples, size_t frames)
{
	return ww_resample(&resampler, samples, frames, put_frame);
}

void audio_sink_stop(void)
{
	if (blk != NULL) {
		k_mem_slab_free(&tx_slab, blk);
		blk = NULL;
	}
	if (running || queued > 0) {
		reset_i2s();
	}
}
