/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Settings service of the mcubus.
 */

#ifndef MCUBUS_SETTINGS_H_
#define MCUBUS_SETTINGS_H_

#include <stdint.h>

/**
 * @defgroup mcubus_settings Settings service
 * @ingroup mcubus
 * @{
 *
 * Every MCU serves its Zephyr settings by name and gets and sets those of the
 * others (CONFIG_MCUBUS_SETTINGS).
 *
 *     request:  MCUBUS_SETTINGS_GET, key, NUL         payload: len, value
 *     request:  MCUBUS_SETTINGS_SET, key, NUL, value  payload: none
 *
 * The target calls settings_runtime_get() / settings_runtime_set() in its I2C
 * interrupt. So settings handlers must be static (SETTINGS_STATIC_HANDLER_DEFINE),
 * only copy memory, and keep their values under a spinlock. A read-only setting
 * refuses with -EACCES. After a set, a work item calls settings_save().
 *
 * Shell: <tt>mcubus get \<addr\> \<key\></tt>,
 * <tt>mcubus set \<addr\> \<key\> \<text\></tt>
 */

/** cmd of the service */
#define MCUBUS_SETTINGS_CMD 0x01

/** Op: get a setting */
#define MCUBUS_SETTINGS_GET 0
/** Op: set a setting */
#define MCUBUS_SETTINGS_SET 1

/** Longest key, NUL included */
#define MCUBUS_SETTINGS_KEY_MAX   CONFIG_MCUBUS_SETTINGS_KEY_MAX
/** Longest value */
#define MCUBUS_SETTINGS_VALUE_MAX CONFIG_MCUBUS_SETTINGS_VALUE_MAX

/**
 * @brief Get a setting of an MCU.
 *
 * @param addr MCU, may be this one.
 * @param key Name of the setting.
 * @param data Value buffer.
 * @param len Size of @p data; a longer value is cut.
 *
 * @return Length of the value, which may exceed @p len, or -errno:
 * @retval -ENOENT No such setting.
 * @retval -EINVAL No handler for the subtree, or key too long.
 * @retval -ENOTSUP The MCU serves no settings.
 */
int mcubus_settings_get(uint8_t addr, const char *key, void *data, uint8_t len);

/**
 * @brief Set a setting of an MCU, which then stores it.
 *
 * @param addr MCU, may be this one.
 * @param key Name of the setting.
 * @param data Value.
 * @param len Length of @p data, up to MCUBUS_SETTINGS_VALUE_MAX.
 *
 * @retval 0 Set.
 * @retval -EACCES Read-only.
 * @retval -errno See mcubus_settings_get().
 */
int mcubus_settings_set(uint8_t addr, const char *key, const void *data, uint8_t len);

/** @} */

#endif /* MCUBUS_SETTINGS_H_ */
