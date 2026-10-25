/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Settings service of the mcubus (CONFIG_MCUBUS_SETTINGS): every MCU serves its own
 * settings (Zephyr settings subsystem) by name, and gets and sets those of the
 * others. Requests of the mcubus (mcubus/mcubus.h), one cmd with an op first:
 *
 *   MCUBUS_SETTINGS_CMD  MCUBUS_SETTINGS_GET, key, NUL          -> status, len, value
 *   MCUBUS_SETTINGS_CMD  MCUBUS_SETTINGS_SET, key, NUL, value   -> status
 *
 * The target runs the request in its interrupt: settings_runtime_get() or
 * settings_runtime_set(). These take no lock in the settings subsystem; they
 * look up the handler and call it. So the settings handlers must be static
 * (SETTINGS_STATIC_HANDLER_DEFINE), only copy memory, and keep their values
 * whole with a spinlock: they run in that interrupt and in threads. A handler
 * refuses to set a read-only setting (-EACCES). After a SET, a work item stores
 * the settings (settings_save()), never the interrupt.
 *
 * With CONFIG_MCUBUS_SHELL, the shell commands "mcubus get" and "mcubus set" do
 * the same.
 */

#ifndef MCUBUS_SETTINGS_H_
#define MCUBUS_SETTINGS_H_

#include <stdint.h>

#define MCUBUS_SETTINGS_CMD 0x01

/* The op, first byte of the data */
#define MCUBUS_SETTINGS_GET 0
#define MCUBUS_SETTINGS_SET 1

#define MCUBUS_SETTINGS_KEY_MAX   CONFIG_MCUBUS_SETTINGS_KEY_MAX /* with the terminating NUL */
#define MCUBUS_SETTINGS_VALUE_MAX CONFIG_MCUBUS_SETTINGS_VALUE_MAX

/*
 * Get setting `key` of the MCU at `addr` into `data`; `addr` may be this MCU.
 * Returns the length of the value (it may be more than `len`), or -errno: -EIO
 * nobody answers at `addr`, -ENOENT no such setting (the handler of the subtree
 * says so), -EINVAL no handler for the subtree at all, or key or value too long,
 * -ENOTSUP the MCU at `addr` serves no settings.
 */
int mcubus_settings_get(uint8_t addr, const char *key, void *data, uint8_t len);

/* Set setting `key` of the MCU at `addr`, which stores it. 0, or -errno: -EACCES read-only. */
int mcubus_settings_set(uint8_t addr, const char *key, const void *data, uint8_t len);

#endif /* MCUBUS_SETTINGS_H_ */
