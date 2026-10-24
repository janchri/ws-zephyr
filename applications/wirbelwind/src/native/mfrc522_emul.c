/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Emulated MFRC522 with one ISO 14443A card for native_sim, so the real
 * driver (rfid/mfrc522.c) and tag reader run in the simulation:
 *   ww tag place <uid>   put a card with a 4, 7 or 10 byte UID on the reader
 *   ww tag remove        take it away
 *
 * Emulated: the registers the driver uses, FIFO, Transceive / CalcCRC /
 * SoftReset, the receive timeout and a card answering WUPA/REQA,
 * anticollision, select (CRC checked) and HLTA.
 */

#define DT_DRV_COMPAT nxp_mfrc522

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "../rfid/mfrc522.h"

#define REG_COMMAND      0x01
#define REG_COM_IRQ      0x04
#define REG_DIV_IRQ      0x05
#define REG_ERROR        0x06
#define REG_FIFO_DATA    0x09
#define REG_FIFO_LEVEL   0x0a
#define REG_CONTROL      0x0c
#define REG_BIT_FRAMING  0x0d
#define REG_CRC_RESULT_H 0x21
#define REG_CRC_RESULT_L 0x22
#define REG_VERSION      0x37
#define NUM_REGS         0x40

#define CMD_IDLE       0x00
#define CMD_CALC_CRC   0x03
#define CMD_TRANSCEIVE 0x0c
#define CMD_SOFT_RESET 0x0f

#define COM_IRQ_TIMER BIT(0)
#define COM_IRQ_IDLE  BIT(4)
#define COM_IRQ_RX    BIT(5)
#define DIV_IRQ_CRC   BIT(2)
#define IRQ_SET       BIT(7)

#define FIFO_SIZE 64

enum card_state {
	CARD_IDLE,
	CARD_READY,
	CARD_ACTIVE,
	CARD_HALT,
};

struct mfrc522_emul_data {
	struct k_mutex lock;
	uint8_t regs[NUM_REGS];
	uint8_t fifo[FIFO_SIZE];
	size_t fifo_len;

	bool card_present;
	enum card_state card_state;
	uint8_t uid[MFRC522_UID_MAX];
	size_t uid_len;
};

/* ISO 14443A CRC_A, low byte first; computed independently of the driver */
static void crc_a(const uint8_t *data, size_t len, uint8_t out[2])
{
	uint16_t crc = 0x6363;

	for (size_t i = 0; i < len; i++) {
		uint8_t b = data[i] ^ (crc & 0xff);

		b ^= b << 4;
		crc = (crc >> 8) ^ ((uint16_t)b << 8) ^ ((uint16_t)b << 3) ^ (b >> 4);
	}
	out[0] = crc & 0xff;
	out[1] = crc >> 8;
}

static int num_levels(size_t uid_len)
{
	return uid_len == 4 ? 1 : (uid_len == 7 ? 2 : 3);
}

/* The 4 bytes a card sends in cascade level `level` (with cascade tag if needed) */
static void level_bytes(const struct mfrc522_emul_data *d, int level, uint8_t out[4])
{
	bool last = (level == num_levels(d->uid_len) - 1);
	const uint8_t *src = &d->uid[3 * level];

	if (last) {
		memcpy(out, src, 4);
	} else {
		out[0] = 0x88;
		memcpy(&out[1], src, 3);
	}
}

/* The card's answer to a frame from the reader; returns its length (0 = silent) */
static size_t card_respond(struct mfrc522_emul_data *d, const uint8_t *rx, size_t len,
			   uint8_t last_bits, uint8_t *tx)
{
	uint8_t part[4], crc[2];
	int level;

	if (!d->card_present) {
		return 0;
	}

	/* REQA / WUPA: 7 bit short frames */
	if (len == 1 && last_bits == 7) {
		if (rx[0] == 0x52 || (rx[0] == 0x26 && d->card_state == CARD_IDLE)) {
			d->card_state = CARD_READY;
			tx[0] = (d->uid_len == 4) ? 0x04 : (d->uid_len == 7 ? 0x44 : 0x84);
			tx[1] = 0x00;
			return 2;
		}
		return 0;
	}

	/* HLTA */
	if (len == 4 && rx[0] == 0x50 && rx[1] == 0x00) {
		crc_a(rx, 2, crc);
		if (crc[0] == rx[2] && crc[1] == rx[3]) {
			d->card_state = CARD_HALT;
		}
		return 0;
	}

	if (d->card_state != CARD_READY || (rx[0] != 0x93 && rx[0] != 0x95 && rx[0] != 0x97)) {
		return 0;
	}
	level = (rx[0] - 0x93) / 2;
	if (level >= num_levels(d->uid_len)) {
		return 0;
	}
	level_bytes(d, level, part);

	/* Anticollision: UID bytes of this level + BCC */
	if (len == 2 && rx[1] == 0x20) {
		memcpy(tx, part, 4);
		tx[4] = part[0] ^ part[1] ^ part[2] ^ part[3];
		return 5;
	}

	/* Select: answer SAK + CRC if UID and CRC match */
	if (len == 9 && rx[1] == 0x70) {
		crc_a(rx, 7, crc);
		if (memcmp(&rx[2], part, 4) != 0 || rx[6] != (part[0] ^ part[1] ^ part[2] ^ part[3]) ||
		    crc[0] != rx[7] || crc[1] != rx[8]) {
			return 0;
		}
		if (level == num_levels(d->uid_len) - 1) {
			d->card_state = CARD_ACTIVE;
			tx[0] = 0x08;
		} else {
			tx[0] = 0x04;
		}
		crc_a(tx, 1, &tx[1]);
		return 3;
	}

	return 0;
}

static void soft_reset(struct mfrc522_emul_data *d)
{
	memset(d->regs, 0, sizeof(d->regs));
	d->regs[REG_VERSION] = 0x92;
	d->fifo_len = 0;
}

static void transceive(struct mfrc522_emul_data *d)
{
	uint8_t answer[FIFO_SIZE];
	size_t n = card_respond(d, d->fifo, d->fifo_len, d->regs[REG_BIT_FRAMING] & 0x07, answer);

	d->fifo_len = 0;
	d->regs[REG_ERROR] = 0;
	if (n == 0) {
		/* Nobody answered: the timer runs out, the command keeps receiving */
		d->regs[REG_COM_IRQ] |= COM_IRQ_TIMER;
		return;
	}
	memcpy(d->fifo, answer, n);
	d->fifo_len = n;
	d->regs[REG_CONTROL] &= ~0x07;
	d->regs[REG_COM_IRQ] |= COM_IRQ_RX | COM_IRQ_IDLE;
}

static void reg_write(struct mfrc522_emul_data *d, uint8_t reg, uint8_t val)
{
	switch (reg) {
	case REG_COMMAND:
		d->regs[reg] = val & 0x0f;
		if ((val & 0x0f) == CMD_SOFT_RESET) {
			soft_reset(d);
		} else if ((val & 0x0f) == CMD_CALC_CRC) {
			uint8_t crc[2];

			crc_a(d->fifo, d->fifo_len, crc);
			d->regs[REG_CRC_RESULT_L] = crc[0];
			d->regs[REG_CRC_RESULT_H] = crc[1];
			d->fifo_len = 0;
			d->regs[REG_DIV_IRQ] |= DIV_IRQ_CRC;
		}
		break;
	case REG_COM_IRQ:
	case REG_DIV_IRQ:
		if (val & IRQ_SET) {
			d->regs[reg] |= val & 0x7f;
		} else {
			d->regs[reg] &= ~val;
		}
		break;
	case REG_FIFO_DATA:
		if (d->fifo_len < FIFO_SIZE) {
			d->fifo[d->fifo_len++] = val;
		}
		break;
	case REG_FIFO_LEVEL:
		if (val & BIT(7)) {
			d->fifo_len = 0;
		}
		break;
	case REG_BIT_FRAMING:
		d->regs[reg] = val;
		if ((val & BIT(7)) && d->regs[REG_COMMAND] == CMD_TRANSCEIVE) {
			transceive(d);
		}
		break;
	default:
		d->regs[reg] = val;
		break;
	}
}

static uint8_t reg_read(struct mfrc522_emul_data *d, uint8_t reg)
{
	uint8_t val;

	switch (reg) {
	case REG_FIFO_DATA:
		if (d->fifo_len == 0) {
			return 0;
		}
		val = d->fifo[0];
		memmove(d->fifo, &d->fifo[1], --d->fifo_len);
		return val;
	case REG_FIFO_LEVEL:
		return d->fifo_len;
	default:
		return d->regs[reg];
	}
}

static int emul_io(const struct emul *target, const struct spi_config *config,
		   const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs)
{
	struct mfrc522_emul_data *d = target->data;
	uint8_t tx[FIFO_SIZE + 2], rx[FIFO_SIZE + 2] = {0};
	size_t len = 0;

	ARG_UNUSED(config);

	/* Flatten the buffers into one byte stream */
	for (size_t i = 0; tx_bufs && i < tx_bufs->count; i++) {
		const struct spi_buf *b = &tx_bufs->buffers[i];

		if (len + b->len > sizeof(tx)) {
			return -EIO;
		}
		if (b->buf) {
			memcpy(&tx[len], b->buf, b->len);
		} else {
			memset(&tx[len], 0, b->len);
		}
		len += b->len;
	}
	if (len == 0) {
		return -EIO;
	}

	k_mutex_lock(&d->lock, K_FOREVER);
	if (tx[0] & 0x80) {
		/* Read: every byte is the next address, the answer comes one byte later */
		for (size_t i = 1; i < len; i++) {
			rx[i] = reg_read(d, (tx[i - 1] >> 1) & 0x3f);
		}
	} else {
		for (size_t i = 1; i < len; i++) {
			reg_write(d, (tx[0] >> 1) & 0x3f, tx[i]);
		}
	}
	k_mutex_unlock(&d->lock);

	for (size_t i = 0, pos = 0; rx_bufs && i < rx_bufs->count; i++) {
		const struct spi_buf *b = &rx_bufs->buffers[i];

		if (b->buf) {
			memcpy(b->buf, &rx[pos], MIN(b->len, len - pos));
		}
		pos += b->len;
	}

	return 0;
}

static const struct spi_emul_api emul_api = {
	.io = emul_io,
};

static int emul_init(const struct emul *target, const struct device *parent)
{
	struct mfrc522_emul_data *d = target->data;

	ARG_UNUSED(parent);
	k_mutex_init(&d->lock);
	soft_reset(d);

	return 0;
}

static struct mfrc522_emul_data emul_data;

EMUL_DT_INST_DEFINE(0, emul_init, &emul_data, NULL, &emul_api, NULL);

/* Shell: put a card on the reader / take it away */

static int cmd_tag_place(const struct shell *sh, size_t argc, char **argv)
{
	const char *hex = argv[1];
	size_t n = strlen(hex) / 2;
	uint8_t uid[MFRC522_UID_MAX];

	if ((n != 4 && n != 7 && n != 10) || strlen(hex) != 2 * n) {
		shell_error(sh, "UID must have 4, 7 or 10 bytes (8, 14 or 20 hex digits)");
		return -EINVAL;
	}
	for (size_t i = 0; i < 2 * n; i++) {
		if (!isxdigit((unsigned char)hex[i])) {
			shell_error(sh, "UID must be hex");
			return -EINVAL;
		}
	}
	for (size_t i = 0; i < n; i++) {
		char byte[3] = {hex[2 * i], hex[2 * i + 1], '\0'};

		uid[i] = strtoul(byte, NULL, 16);
	}

	k_mutex_lock(&emul_data.lock, K_FOREVER);
	memcpy(emul_data.uid, uid, n);
	emul_data.uid_len = n;
	emul_data.card_present = true;
	emul_data.card_state = CARD_IDLE;
	k_mutex_unlock(&emul_data.lock);

	return 0;
}

static int cmd_tag_remove(const struct shell *sh, size_t argc, char **argv)
{
	k_mutex_lock(&emul_data.lock, K_FOREVER);
	emul_data.card_present = false;
	emul_data.card_state = CARD_IDLE;
	k_mutex_unlock(&emul_data.lock);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_tag,
	SHELL_CMD_ARG(place, NULL, "Put a card on the emulated reader: place <uid-hex>",
		      cmd_tag_place, 2, 0),
	SHELL_CMD_ARG(remove, NULL, "Take the card away", cmd_tag_remove, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((ww), tag, &sub_tag, "Card on the emulated RFID reader", NULL, 1, 0);
