/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * X-Powers AC101 codec. Registers are 16 bit, big endian, behind an 8 bit
 * register address. The configuration is the one the Arduino version of the
 * box used: PLL from MCLK = 256 x 44.1 kHz, I2S slave, 16 bit, DAC to the
 * headphone and speaker outputs.
 */

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ac101.h"

LOG_MODULE_DECLARE(ww, CONFIG_WW_LOG_LEVEL);

#define AC101_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(x_powers_ac101)

/* Registers */
#define CHIP_AUDIO_RS    0x00
#define PLL_CTRL1        0x01
#define PLL_CTRL2        0x02
#define SYSCLK_CTRL      0x03
#define MOD_CLK_ENA      0x04
#define MOD_RST_CTRL     0x05
#define I2S_SR_CTRL      0x06
#define I2S1LCK_CTRL     0x10
#define I2S1_SDOUT_CTRL  0x11
#define I2S1_SDIN_CTRL   0x12
#define I2S1_MXR_SRC     0x13
#define DAC_DIG_CTRL     0x48
#define DAC_MXR_SRC      0x4c
#define OMIXER_DACA_CTRL 0x53
#define OMIXER_SR        0x54
#define HPOUT_CTRL       0x56
#define SPKOUT_CTRL      0x58

/* Writing this to CHIP_AUDIO_RS resets the chip, reading it back gives the ID */
#define CHIP_RESET 0x0123
#define CHIP_ID    0x0101

/* I2S1LCK_CTRL: slave, BCLK = SYSCLK/8, LRCK = BCLK/32, 16 bit words, I2S format */
#define I2S1LCK_SLAVE   BIT(15)
#define I2S1LCK_BCLK_8  (0x4 << 9)
#define I2S1LCK_LRCK_32 (0x1 << 6)
#define I2S1LCK_16BIT   (0x1 << 4)
#define I2S1LCK_FMT_I2S (0x0 << 2)

#define I2S_SR_44100 0x7000

#define HPOUT_VOL_SHIFT 4

/* Output stages on (headphone + speaker), volume fields cleared */
#define HPOUT_ON   0xf800
#define SPKOUT_ON  0xeaa0
#define SPKOUT_OFF 0xe880

static const struct i2c_dt_spec i2c = I2C_DT_SPEC_GET(AC101_NODE);
static const struct gpio_dt_spec pa = GPIO_DT_SPEC_GET_OR(AC101_NODE, pa_gpios, {0});

static uint16_t hp_vol;
static uint16_t spk_vol;

static int reg_write(uint8_t reg, uint16_t val)
{
	uint8_t buf[3] = {reg, val >> 8, val & 0xff};

	return i2c_write_dt(&i2c, buf, sizeof(buf));
}

static int reg_read(uint8_t reg, uint16_t *val)
{
	uint8_t buf[2];
	int ret = i2c_write_read_dt(&i2c, &reg, 1, buf, sizeof(buf));

	*val = (buf[0] << 8) | buf[1];

	return ret;
}

int ac101_init(void)
{
	static const struct {
		uint8_t reg;
		uint16_t val;
	} init_seq[] = {
		{SPKOUT_CTRL, SPKOUT_OFF},
		/* PLL from MCLK = 256 x 44.1 kHz, SYSCLK from the PLL */
		{PLL_CTRL1, 0x014f},
		{PLL_CTRL2, 0x8600},
		{SYSCLK_CTRL, 0x8b08},
		/* Clocks and resets of the I2S1 and DAC modules */
		{MOD_CLK_ENA, 0x800c},
		{MOD_RST_CTRL, 0x800c},
		{I2S_SR_CTRL, I2S_SR_44100},
		{I2S1LCK_CTRL,
		 I2S1LCK_SLAVE | I2S1LCK_BCLK_8 | I2S1LCK_LRCK_32 | I2S1LCK_16BIT | I2S1LCK_FMT_I2S},
		{I2S1_SDOUT_CTRL, 0xc000},
		{I2S1_SDIN_CTRL, 0xc000},
		{I2S1_MXR_SRC, 0x2200},
		/* DAC -> output mixer */
		{DAC_MXR_SRC, 0xcc00},
		{DAC_DIG_CTRL, 0x8000},
		{OMIXER_SR, 0x0081},
		{OMIXER_DACA_CTRL, 0xff80},
	};
	uint16_t id;
	int ret;

	if (!i2c_is_ready_dt(&i2c)) {
		LOG_ERR("AC101: I2C not ready");
		return -ENODEV;
	}

	if (pa.port != NULL) {
		ret = gpio_pin_configure_dt(&pa, GPIO_OUTPUT_INACTIVE);
		if (ret) {
			return ret;
		}
	}

	ret = reg_write(CHIP_AUDIO_RS, CHIP_RESET);
	if (ret) {
		LOG_ERR("AC101 not responding: %d", ret);
		return ret;
	}
	k_msleep(100);

	ret = reg_read(CHIP_AUDIO_RS, &id);
	if (ret || id != CHIP_ID) {
		LOG_ERR("AC101: unexpected chip id 0x%04x (%d)", id, ret);
		return -ENODEV;
	}

	for (size_t i = 0; i < ARRAY_SIZE(init_seq); i++) {
		ret = reg_write(init_seq[i].reg, init_seq[i].val);
		if (ret) {
			LOG_ERR("AC101: writing 0x%02x failed: %d", init_seq[i].reg, ret);
			return ret;
		}
	}

	/* The output mixer needs some time before the outputs are enabled */
	k_msleep(100);

	LOG_INF("AC101 ready");

	return 0;
}

int ac101_set_output_volume(uint8_t headphone, uint8_t speaker)
{
	hp_vol = MIN(headphone, 63);
	spk_vol = MIN(speaker, 31);

	return 0;
}

int ac101_enable_output(bool enable)
{
	int ret;

	if (enable) {
		ret = reg_write(HPOUT_CTRL, HPOUT_ON | (hp_vol << HPOUT_VOL_SHIFT));
		if (ret == 0) {
			ret = reg_write(SPKOUT_CTRL, SPKOUT_ON | spk_vol);
		}
	} else {
		ret = reg_write(SPKOUT_CTRL, SPKOUT_OFF);
	}

	if (pa.port != NULL) {
		gpio_pin_set_dt(&pa, enable);
	}

	return ret;
}
