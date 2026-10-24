/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NXP MFRC522 RFID reader. Only what the box needs: detect an ISO 14443A
 * card and read its UID (cascade levels 1-3, i.e. 4, 7 or 10 byte UIDs).
 *
 * SPI framing (datasheet 8.1.2): the first byte is the register address
 * shifted left by one, bit 7 set for reading. Reading n bytes sends n
 * addresses followed by a 0; byte i+1 of the answer belongs to address i.
 */

#define DT_DRV_COMPAT nxp_mfrc522

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "mfrc522.h"

LOG_MODULE_REGISTER(mfrc522, CONFIG_WW_LOG_LEVEL);

/* Registers */
#define REG_COMMAND      0x01
#define REG_COM_IRQ      0x04
#define REG_DIV_IRQ      0x05
#define REG_ERROR        0x06
#define REG_FIFO_DATA    0x09
#define REG_FIFO_LEVEL   0x0a
#define REG_BIT_FRAMING  0x0d
#define REG_COLL         0x0e
#define REG_MODE         0x11
#define REG_TX_MODE      0x12
#define REG_RX_MODE      0x13
#define REG_TX_CONTROL   0x14
#define REG_TX_ASK       0x15
#define REG_CRC_RESULT_H 0x21
#define REG_CRC_RESULT_L 0x22
#define REG_MOD_WIDTH    0x24
#define REG_T_MODE       0x2a
#define REG_T_PRESCALER  0x2b
#define REG_T_RELOAD_H   0x2c
#define REG_T_RELOAD_L   0x2d
#define REG_VERSION      0x37

/* Commands, and bits of the registers above */
#define CMD_IDLE           0x00
#define CMD_CALC_CRC       0x03
#define CMD_TRANSCEIVE     0x0c
#define CMD_SOFT_RESET     0x0f
#define COMMAND_POWER_DOWN BIT(4)

#define COM_IRQ_TIMER BIT(0)
#define COM_IRQ_IDLE  BIT(4)
#define COM_IRQ_RX    BIT(5)
#define COM_IRQ_ALL   0x7f
#define DIV_IRQ_CRC   BIT(2)

#define ERROR_PROTOCOL    BIT(0)
#define ERROR_PARITY      BIT(1)
#define ERROR_COLLISION   BIT(3)
#define ERROR_BUFFER_OVFL BIT(4)

#define FIFO_FLUSH          BIT(7)
#define FIFO_SIZE           64
#define BIT_FRAMING_START   BIT(7)
#define COLL_VALUES_AFTER   BIT(7)
#define TX_CONTROL_ANTENNAS 0x03

/* ISO 14443A card commands */
#define PICC_WUPA         0x52
#define PICC_SEL_CL1      0x93 /* CL2 = 0x95, CL3 = 0x97 */
#define PICC_NVB_ANTICOLL 0x20
#define PICC_NVB_SELECT   0x70
#define PICC_HLTA         0x50
#define PICC_CASCADE_TAG  0x88
#define SAK_UID_INCOMPLETE BIT(2)

/* The chip timer ends a transceive after 25 ms without answer, see init */
#define TRANSCEIVE_TIMEOUT_MS 40

struct mfrc522_config {
	struct spi_dt_spec spi;
	struct gpio_dt_spec reset;
};

static int reg_write_buf(const struct device *dev, uint8_t reg, const uint8_t *val, size_t len)
{
	const struct mfrc522_config *cfg = dev->config;
	uint8_t addr = (reg << 1) & 0x7e;
	const struct spi_buf bufs[] = {
		{.buf = &addr, .len = 1},
		{.buf = (void *)val, .len = len},
	};
	const struct spi_buf_set tx = {.buffers = bufs, .count = ARRAY_SIZE(bufs)};

	return spi_write_dt(&cfg->spi, &tx);
}

static int reg_write(const struct device *dev, uint8_t reg, uint8_t val)
{
	return reg_write_buf(dev, reg, &val, 1);
}

static int reg_read_buf(const struct device *dev, uint8_t reg, uint8_t *val, size_t len)
{
	const struct mfrc522_config *cfg = dev->config;
	uint8_t tx[FIFO_SIZE + 1];
	uint8_t rx[FIFO_SIZE + 1];
	const struct spi_buf tx_buf = {.buf = tx, .len = len + 1};
	const struct spi_buf rx_buf = {.buf = rx, .len = len + 1};
	const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
	const struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};
	int ret;

	if (len > FIFO_SIZE) {
		return -EINVAL;
	}

	memset(tx, ((reg << 1) & 0x7e) | 0x80, len);
	tx[len] = 0;

	ret = spi_transceive_dt(&cfg->spi, &tx_set, &rx_set);
	if (ret == 0) {
		memcpy(val, &rx[1], len);
	}

	return ret;
}

static int reg_read(const struct device *dev, uint8_t reg, uint8_t *val)
{
	return reg_read_buf(dev, reg, val, 1);
}

static int reg_update(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t val)
{
	uint8_t old;
	int ret = reg_read(dev, reg, &old);

	if (ret) {
		return ret;
	}

	return reg_write(dev, reg, (old & ~mask) | (val & mask));
}

/* CRC_A over data, computed by the chip's coprocessor; low byte first */
static int calc_crc(const struct device *dev, const uint8_t *data, size_t len, uint8_t crc[2])
{
	int64_t end = k_uptime_get() + 10;
	uint8_t irq;
	int ret;

	ret = reg_write(dev, REG_COMMAND, CMD_IDLE);
	ret = ret ? ret : reg_write(dev, REG_DIV_IRQ, DIV_IRQ_CRC);
	ret = ret ? ret : reg_write(dev, REG_FIFO_LEVEL, FIFO_FLUSH);
	ret = ret ? ret : reg_write_buf(dev, REG_FIFO_DATA, data, len);
	ret = ret ? ret : reg_write(dev, REG_COMMAND, CMD_CALC_CRC);
	if (ret) {
		return ret;
	}

	do {
		ret = reg_read(dev, REG_DIV_IRQ, &irq);
		if (ret) {
			return ret;
		}
		if (irq & DIV_IRQ_CRC) {
			reg_write(dev, REG_COMMAND, CMD_IDLE);
			ret = reg_read(dev, REG_CRC_RESULT_L, &crc[0]);
			return ret ? ret : reg_read(dev, REG_CRC_RESULT_H, &crc[1]);
		}
	} while (k_uptime_get() < end);

	return -ETIMEDOUT;
}

/*
 * Send a frame to the card and receive its answer. tx_last_bits: number of
 * bits of the last byte to send (0 = all 8).
 */
static int transceive(const struct device *dev, const uint8_t *tx, size_t tx_len,
		      uint8_t tx_last_bits, uint8_t *rx, size_t *rx_len)
{
	int64_t end = k_uptime_get() + TRANSCEIVE_TIMEOUT_MS;
	uint8_t irq, err, level;
	int ret;

	ret = reg_write(dev, REG_COMMAND, CMD_IDLE);
	ret = ret ? ret : reg_write(dev, REG_COM_IRQ, COM_IRQ_ALL);
	ret = ret ? ret : reg_write(dev, REG_FIFO_LEVEL, FIFO_FLUSH);
	ret = ret ? ret : reg_write_buf(dev, REG_FIFO_DATA, tx, tx_len);
	ret = ret ? ret : reg_write(dev, REG_BIT_FRAMING, tx_last_bits);
	ret = ret ? ret : reg_write(dev, REG_COMMAND, CMD_TRANSCEIVE);
	ret = ret ? ret : reg_update(dev, REG_BIT_FRAMING, BIT_FRAMING_START, BIT_FRAMING_START);
	if (ret) {
		return ret;
	}

	for (;;) {
		ret = reg_read(dev, REG_COM_IRQ, &irq);
		if (ret) {
			return ret;
		}
		if (irq & (COM_IRQ_RX | COM_IRQ_IDLE)) {
			break;
		}
		if (irq & COM_IRQ_TIMER) {
			return -ENOENT;
		}
		if (k_uptime_get() > end) {
			return -ETIMEDOUT;
		}
		k_msleep(1);
	}

	reg_update(dev, REG_BIT_FRAMING, BIT_FRAMING_START, 0);

	ret = reg_read(dev, REG_ERROR, &err);
	if (ret) {
		return ret;
	}
	if (err & ERROR_COLLISION) {
		return -EBUSY;
	}
	if (err & (ERROR_BUFFER_OVFL | ERROR_PARITY | ERROR_PROTOCOL)) {
		return -EIO;
	}

	ret = reg_read(dev, REG_FIFO_LEVEL, &level);
	if (ret) {
		return ret;
	}
	if (level > *rx_len) {
		return -ENOBUFS;
	}
	*rx_len = level;

	return reg_read_buf(dev, REG_FIFO_DATA, rx, level);
}

/* Anticollision and select of one cascade level; returns the SAK */
static int select_level(const struct device *dev, int level, uint8_t uid_part[4], uint8_t *sak)
{
	uint8_t frame[9] = {PICC_SEL_CL1 + 2 * level, PICC_NVB_ANTICOLL};
	uint8_t answer[3], crc[2];
	size_t len = 5;
	int ret;

	/* Answer: 4 UID bytes and their XOR (BCC) */
	ret = transceive(dev, frame, 2, 0, &frame[2], &len);
	if (ret) {
		return ret;
	}
	if (len != 5 || (frame[2] ^ frame[3] ^ frame[4] ^ frame[5]) != frame[6]) {
		return -EIO;
	}

	frame[1] = PICC_NVB_SELECT;
	ret = calc_crc(dev, frame, 7, &frame[7]);
	if (ret) {
		return ret;
	}

	/* Answer: SAK and its CRC */
	len = sizeof(answer);
	ret = transceive(dev, frame, sizeof(frame), 0, answer, &len);
	if (ret) {
		return ret;
	}
	if (len != 3) {
		return -EIO;
	}
	ret = calc_crc(dev, answer, 1, crc);
	if (ret) {
		return ret;
	}
	if (crc[0] != answer[1] || crc[1] != answer[2]) {
		return -EIO;
	}

	memcpy(uid_part, &frame[2], 4);
	*sak = answer[0];

	return 0;
}

int mfrc522_read_uid(const struct device *dev, uint8_t uid[MFRC522_UID_MAX], size_t *len)
{
	uint8_t wupa = PICC_WUPA;
	uint8_t atqa[2];
	uint8_t hlta[4] = {PICC_HLTA, 0x00};
	uint8_t part[4], sak = 0;
	size_t n = sizeof(atqa);
	int ret;

	/* Clear received bits after a collision */
	ret = reg_update(dev, REG_COLL, COLL_VALUES_AFTER, 0);
	if (ret) {
		return ret;
	}

	/* WUPA is a short frame of 7 bits; it also wakes up halted cards */
	ret = transceive(dev, &wupa, 1, 7, atqa, &n);
	if (ret) {
		return ret;
	}
	if (n != sizeof(atqa)) {
		return -EIO;
	}

	*len = 0;
	for (int level = 0; level < 3; level++) {
		ret = select_level(dev, level, part, &sak);
		if (ret) {
			return ret;
		}

		/* A cascade tag means the UID continues on the next level */
		if (part[0] == PICC_CASCADE_TAG) {
			memcpy(&uid[*len], &part[1], 3);
			*len += 3;
		} else {
			memcpy(&uid[*len], part, 4);
			*len += 4;
		}
		if (!(sak & SAK_UID_INCOMPLETE)) {
			break;
		}
	}
	if (sak & SAK_UID_INCOMPLETE) {
		return -EIO;
	}

	/* Halt the card; it does not answer, so -ENOENT is the good case */
	if (calc_crc(dev, hlta, 2, &hlta[2]) == 0) {
		n = 0;
		transceive(dev, hlta, sizeof(hlta), 0, NULL, &n);
	}

	return 0;
}

static int mfrc522_init(const struct device *dev)
{
	const struct mfrc522_config *cfg = dev->config;
	static const uint8_t init_seq[][2] = {
		{REG_TX_MODE, 0x00},
		{REG_RX_MODE, 0x00},
		{REG_MOD_WIDTH, 0x26},
		/* Timer: auto start after sending, 13.56 MHz / (2 * 169 + 1) = 40 kHz,
		 * reload 1000 -> a card has 25 ms to answer
		 */
		{REG_T_MODE, 0x80},
		{REG_T_PRESCALER, 0xa9},
		{REG_T_RELOAD_H, 0x03},
		{REG_T_RELOAD_L, 0xe8},
		/* 100 % ASK modulation, CRC preset 0x6363 (ISO 14443A) */
		{REG_TX_ASK, 0x40},
		{REG_MODE, 0x3d},
	};
	uint8_t val = 0;
	int ret;

	if (!spi_is_ready_dt(&cfg->spi)) {
		return -ENODEV;
	}

	if (cfg->reset.port != NULL) {
		ret = gpio_pin_configure_dt(&cfg->reset, GPIO_OUTPUT_ACTIVE);
		if (ret) {
			return ret;
		}
		k_msleep(1);
		gpio_pin_set_dt(&cfg->reset, 0);
		k_msleep(50);
	}

	ret = reg_write(dev, REG_COMMAND, CMD_SOFT_RESET);
	if (ret) {
		return ret;
	}
	for (int i = 0; i < 10; i++) {
		k_msleep(10);
		ret = reg_read(dev, REG_COMMAND, &val);
		if (ret == 0 && !(val & COMMAND_POWER_DOWN)) {
			break;
		}
	}

	ret = reg_read(dev, REG_VERSION, &val);
	if (ret || val == 0x00 || val == 0xff) {
		LOG_ERR("No MFRC522 found (version 0x%02x, %d)", val, ret);
		return -ENODEV;
	}

	for (size_t i = 0; i < ARRAY_SIZE(init_seq); i++) {
		ret = reg_write(dev, init_seq[i][0], init_seq[i][1]);
		if (ret) {
			return ret;
		}
	}

	ret = reg_update(dev, REG_TX_CONTROL, TX_CONTROL_ANTENNAS, TX_CONTROL_ANTENNAS);
	if (ret) {
		return ret;
	}

	LOG_INF("MFRC522 version 0x%02x", val);

	return 0;
}

#define MFRC522_DEFINE(n)                                                                          \
	static const struct mfrc522_config mfrc522_config_##n = {                                  \
		.spi = SPI_DT_SPEC_INST_GET(n, SPI_OP_MODE_MASTER | SPI_WORD_SET(8)),            \
		.reset = GPIO_DT_SPEC_INST_GET_OR(n, reset_gpios, {0}),                            \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, mfrc522_init, NULL, NULL, &mfrc522_config_##n, POST_KERNEL,       \
			      CONFIG_WW_RFID_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(MFRC522_DEFINE)
