/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Statistics and test bench helpers of the native_sim multi-master I2C port
 * (dts/bindings/zephyr,i2c-mm-emul.yaml)
 */

#ifndef I2C_MM_EMUL_H_
#define I2C_MM_EMUL_H_

#include <stdint.h>
#include <zephyr/device.h>

struct i2c_mm_emul_stats {
	uint32_t arb_lost;     /* transfers lost to another master (-EAGAIN) */
	uint32_t bus_timeouts; /* gave up waiting for a busy bus (-EBUSY) */
	uint32_t nacks;        /* address or data byte not acknowledged */
	uint32_t irqs;         /* target interrupts handled by this port */
	uint32_t recoveries;   /* stuck bus freed by this port */
};

void i2c_mm_emul_get_stats(const struct device *dev, struct i2c_mm_emul_stats *stats);

/*
 * Test bench only, a real MCU has nothing like it: the MCUs are separate
 * processes that start at different times.
 *
 * Wait until `count` MCUs (this one included) are on the bus and have a target
 * address. Returns 0, or -ETIMEDOUT.
 */
int i2c_mm_emul_wait_nodes(const struct device *dev, int count);

/*
 * Wait for the start of round `round`. Round 0 starts one second after the first
 * MCU joined the bus (the others have started by then), every later round
 * `period_ms` after the one before. All MCUs get the same moments, so that
 * their transfers collide. A round that is already due returns at once. Waits
 * on the wall clock: needs native_sim to run in real time (the default).
 */
void i2c_mm_emul_wait_round(const struct device *dev, uint32_t round, uint32_t period_ms);

/*
 * Test bench only: a target holds SDA low, as one that was reset in the middle of
 * a read. The next controller that wants the bus recovers it first.
 */
void i2c_mm_emul_stick_bus(const struct device *dev);

#endif /* I2C_MM_EMUL_H_ */
