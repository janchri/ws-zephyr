/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/i2c/i2c_mm.h>

LOG_MODULE_REGISTER(i2c_mm, CONFIG_I2C_MM_LOG_LEVEL);

void i2c_mm_get_stats(const struct i2c_mm *mm, struct i2c_mm_stats *stats)
{
	*stats = mm->stats;
}

static uint8_t next_byte(struct i2c_mm *mm)
{
	return mm->tx_pos < mm->tx_len ? mm->tx[mm->tx_pos++] : 0xff;
}

/* A request whose read never came is not run, then or later */
static int forget_request(struct i2c_target_config *cfg)
{
	struct i2c_mm *mm = CONTAINER_OF(cfg, struct i2c_mm, cfg);

	mm->rx_len = 0;
	return 0;
}

static int write_received(struct i2c_target_config *cfg, uint8_t val)
{
	struct i2c_mm *mm = CONTAINER_OF(cfg, struct i2c_mm, cfg);

	if (mm->rx_len >= sizeof(mm->rx)) {
		return -ENOMEM;
	}
	mm->rx[mm->rx_len++] = val;
	return 0;
}

static int read_requested(struct i2c_target_config *cfg, uint8_t *val)
{
	struct i2c_mm *mm = CONTAINER_OF(cfg, struct i2c_mm, cfg);

	mm->tx_len = mm->cb(mm, mm->rx, mm->rx_len, mm->tx, sizeof(mm->tx));
	mm->tx_pos = 0;
	mm->rx_len = 0; /* a read alone must not run it again */
	mm->stats.served++;
	*val = next_byte(mm);
	return 0;
}

static int read_processed(struct i2c_target_config *cfg, uint8_t *val)
{
	struct i2c_mm *mm = CONTAINER_OF(cfg, struct i2c_mm, cfg);

	*val = next_byte(mm);
	return 0;
}

static const struct i2c_target_callbacks target_cb = {
	.write_requested = forget_request,
	.write_received = write_received,
	.read_requested = read_requested,
	.read_processed = read_processed,
	.stop = forget_request,
};

int i2c_mm_init(struct i2c_mm *mm, const struct device *i2c, uint16_t addr, i2c_mm_request_cb cb)
{
	int ret;

	if (!device_is_ready(i2c)) {
		LOG_ERR("%s not ready", i2c->name);
		return -ENODEV;
	}
	mm->i2c = i2c;
	mm->cb = cb;
	k_mutex_init(&mm->lock);
	mm->cfg.address = addr;
	mm->cfg.callbacks = &target_cb;
	ret = i2c_target_register(i2c, &mm->cfg);
	if (ret) {
		LOG_ERR("%s: cannot serve at 0x%02x: %d", i2c->name, addr, ret);
	}
	return ret;
}

static void transfer_self(struct i2c_mm *mm, const uint8_t *req, size_t req_len, uint8_t *rsp,
			  size_t rsp_len)
{
	uint8_t out[I2C_MM_MSG_MAX];
	size_t len = mm->cb(mm, req, req_len, out, sizeof(out));

	memset(&out[len], 0xff, sizeof(out) - len);
	memcpy(rsp, out, rsp_len);
}

int i2c_mm_transfer(struct i2c_mm *mm, uint16_t addr, const void *req, size_t req_len, void *rsp,
		    size_t rsp_len)
{
	int64_t end = k_uptime_get() + CONFIG_I2C_MM_RETRY_MS;

	if (req_len > I2C_MM_MSG_MAX || rsp_len > I2C_MM_MSG_MAX) {
		return -EINVAL;
	}
	if (addr == i2c_mm_addr(mm)) {
		transfer_self(mm, req, req_len, rsp, rsp_len);
		return 0;
	}

	for (uint32_t i = 1;; i++) {
		int ret;

		/* re-registering must not hit another thread's transfer */
		k_mutex_lock(&mm->lock, K_FOREVER);
		ret = i2c_write_read(mm->i2c, addr, req, req_len, rsp, rsp_len);
		if (IS_ENABLED(CONFIG_I2C_MM_TARGET_REREGISTER)) {
			(void)i2c_target_register(mm->i2c, &mm->cfg);
		}
		if (ret == 0) {
			mm->stats.sent++;
		} else if (ret == -EAGAIN || ret == -EBUSY) {
			mm->stats.retries++;
		}
		k_mutex_unlock(&mm->lock);

		if ((ret != -EAGAIN && ret != -EBUSY) || k_uptime_get() >= end) {
			return ret;
		}
		LOG_WRN("0x%02x: request to 0x%02x attempt %u: %s, backing off", i2c_mm_addr(mm),
			addr, i, ret == -EAGAIN ? "lost arbitration" : "bus busy");
		/* out of the winner's START window, then wait for its STOP */
		k_sleep(K_MSEC(2 * MIN(i, 4)));
	}
}
