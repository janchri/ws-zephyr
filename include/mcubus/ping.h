/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ping service of the mcubus (CONFIG_MCUBUS_PING): does an MCU answer, and does it
 * speak this MCU's protocol? Request of the mcubus (mcubus/mcubus.h):
 *
 *   MCUBUS_PING_CMD  (no data)  -> status, MCUBUS_PROTOCOL_VERSION, MCUBUS_MSG_MAX
 *
 * With CONFIG_MCUBUS_SHELL: "mcubus ping <addr>".
 */

#ifndef MCUBUS_PING_H_
#define MCUBUS_PING_H_

#include <stdint.h>

#define MCUBUS_PING_CMD 0x00

struct mcubus_info {
	uint8_t version; /* MCUBUS_PROTOCOL_VERSION of that MCU */
	uint8_t msg_max; /* its MCUBUS_MSG_MAX */
};

/*
 * Ask the MCU at `addr` for its protocol version and MCUBUS_MSG_MAX. Returns 0,
 * -EPROTO if they differ from this MCU's (`info` is filled in anyway), or the
 * -errno of the request.
 */
int mcubus_ping(uint8_t addr, struct mcubus_info *info);

#endif /* MCUBUS_PING_H_ */
