/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Messages between equal MCUs on a multi-master I2C bus (CONFIG_I2C_MM).
 *
 * Every MCU is an I2C target at its own address and, whenever it sends, a
 * controller. A message and its answer are one I2C transfer, so no other master
 * can come in between:
 *
 *   write:  request
 *   repeated START
 *   read:   response
 *
 * The link knows nothing about what the bytes mean. On the target side it
 * collects the request and, when the read begins, asks its owner for the
 * response (i2c_mm_request_cb, in the I2C interrupt). On the controller side it
 * sends a request and reads the response, and retries after an arbitration loss
 * or a busy bus.
 *
 * A request runs at most once: the target forgets it once the callback has
 * answered it, and at every STOP. A read without a request (a bus scan, a
 * controller that lost its place) gets the callback with an empty request, and
 * a request whose read never came (its controller gave up) does not run at all.
 *
 * Controllers that start at the same moment arbitrate, and the lowest frame
 * wins: address byte, then the request bytes.
 *
 * Built on the Zephyr I2C API only (i2c_transfer(), i2c_target_register()), so
 * it runs on any I2C driver that does controller and target on one block and
 * reports a lost arbitration as -EAGAIN.
 */

#ifndef I2C_MM_H_
#define I2C_MM_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

/* Longest request and longest response */
#define I2C_MM_MSG_MAX CONFIG_I2C_MM_MSG_MAX

struct i2c_mm;

/*
 * The response to request `req`: write it to `rsp` (room for `rsp_max` bytes) and
 * return its length. Runs in the I2C target interrupt, or in the sending thread
 * for a request of the MCU to itself: short, and only copying memory.
 */
typedef size_t (*i2c_mm_request_cb)(struct i2c_mm *mm, const uint8_t *req, size_t req_len,
				    uint8_t *rsp, size_t rsp_max);

struct i2c_mm_stats {
	uint32_t sent;    /* requests to others that got through */
	uint32_t served;  /* requests of others that this MCU has answered */
	uint32_t retries; /* attempts lost to other masters, or to a busy bus */
};

/* One per MCU; fill in nothing, i2c_mm_init() does */
struct i2c_mm {
	const struct device *i2c;
	i2c_mm_request_cb cb;
	struct i2c_mm_stats stats;

	/* the request the target interrupt is in */
	struct i2c_target_config cfg;
	uint8_t rx[I2C_MM_MSG_MAX];
	uint8_t rx_len;
	uint8_t tx[I2C_MM_MSG_MAX];
	uint8_t tx_len;
	uint8_t tx_pos;
};

/* Serve requests at `addr` on the bus `i2c` from now on. 0 or -errno. */
int i2c_mm_init(struct i2c_mm *mm, const struct device *i2c, uint16_t addr, i2c_mm_request_cb cb);

/*
 * Send `req` to the MCU at `addr` and read `rsp_len` bytes of its response into
 * `rsp`; bytes the target did not answer read as 0xff. `addr` may be this MCU:
 * then the callback runs here, without the bus. An arbitration loss or a busy
 * bus is retried with a backoff, for up to CONFIG_I2C_MM_RETRY_MS. Returns 0 or
 * the -errno of the transfer: -EIO nobody answers at `addr` (or the request is
 * too long), -EAGAIN / -EBUSY the bus stayed taken, -EINVAL longer than
 * I2C_MM_MSG_MAX.
 *
 * If the read of the response fails, the request may have run on the target or
 * not.
 */
int i2c_mm_transfer(struct i2c_mm *mm, uint16_t addr, const void *req, size_t req_len, void *rsp,
		    size_t rsp_len);

static inline uint16_t i2c_mm_addr(const struct i2c_mm *mm)
{
	return mm->cfg.address;
}

void i2c_mm_get_stats(const struct i2c_mm *mm, struct i2c_mm_stats *stats);

#endif /* I2C_MM_H_ */
