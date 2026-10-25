/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Ping service of the mcubus.
 */

#ifndef MCUBUS_PING_H_
#define MCUBUS_PING_H_

#include <stdint.h>

/**
 * @defgroup mcubus_ping Ping service
 * @ingroup mcubus
 * @{
 *
 * Does an MCU answer, and with this MCU's protocol? Enabled by default
 * (CONFIG_MCUBUS_PING): every MCU should answer it.
 *
 *     request:  no data
 *     payload:  MCUBUS_PROTOCOL_VERSION, MCUBUS_MSG_MAX
 *
 * Shell: <tt>mcubus ping \<addr\></tt>
 */

/** cmd of the service */
#define MCUBUS_PING_CMD 0x00

/** @brief What an MCU answers */
struct mcubus_info {
	/** Its MCUBUS_PROTOCOL_VERSION */
	uint8_t version;
	/** Its MCUBUS_MSG_MAX */
	uint8_t msg_max;
};

/**
 * @brief Ping an MCU.
 *
 * @param addr MCU to ping.
 * @param info What it answered.
 *
 * @retval 0 Same protocol and limits as this MCU.
 * @retval -EPROTO It answered, but differs; @p info is filled in.
 * @retval -errno See mcubus_request().
 */
int mcubus_ping(uint8_t addr, struct mcubus_info *info);

/** @} */

#endif /* MCUBUS_PING_H_ */
