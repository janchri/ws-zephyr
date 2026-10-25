/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Requests between the MCUs on a multi-master I2C bus of equal peers.
 *
 * This MCU is the devicetree node /chosen zephyr,mcubus-self: a
 * "zephyr,mcubus-node" child of its I2C bus, at its address. From boot on it serves requests at
 * that address as an I2C target, and it sends requests to the other MCUs as a controller
 * (mcubus_request()).
 *
 * A request is one I2C transfer, so no other master can come in between:
 *
 *   write:  cmd, data
 *   repeated START
 *   read:   response; its first byte is a status, negative is -errno
 *
 * The cmd picks a service (MCUBUS_SERVICE_DEFINE); a cmd that no service has
 * gets -ENOTSUP. The target runs the service in its interrupt, when the read
 * begins, so a service must be short and only copy memory. A request of the MCU
 * to itself does not go over the bus: the service runs in the calling thread.
 *
 * Controllers that start at the same moment arbitrate, and the lowest frame wins:
 * address byte, then cmd. A lower cmd has the higher priority.
 */

#ifndef MCUBUS_H_
#define MCUBUS_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/sys/iterable_sections.h>

/* Longest request (cmd and data) and longest response (status included) */
#define MCUBUS_MSG_MAX CONFIG_MCUBUS_MSG_MAX

/* Version of the requests below; every MCU on a bus needs the same */
#define MCUBUS_PROTOCOL_VERSION 1

/* The services, in order of priority */
#define MCUBUS_CMD_PING         0x00 /* mcubus_ping() */
#define MCUBUS_CMD_SETTINGS_GET 0x01 /* mcubus/settings.h */
#define MCUBUS_CMD_SETTINGS_SET 0x02
#define MCUBUS_CMD_UART         0x10 /* drivers/serial/uart_mcubus.c */

/* This MCU in the devicetree */
#define MCUBUS_SELF DT_CHOSEN(zephyr_mcubus_self)

struct mcubus_service {
	uint8_t cmd;
	/*
	 * `req` is the data after the cmd. Write the payload of the response to `rsp`
	 * (it has room for MCUBUS_MSG_MAX - 1 bytes) and return its length, or return
	 * -errno. The status byte in front of the payload is mcubus's: 0, or that
	 * -errno alone.
	 */
	int (*handle)(const uint8_t *req, size_t req_len, uint8_t *rsp);
};

#define MCUBUS_SERVICE_DEFINE(name, _cmd, _handle)                                                 \
	static const STRUCT_SECTION_ITERABLE(mcubus_service, name) = {                             \
		.cmd = _cmd,                                                                       \
		.handle = _handle,                                                                 \
	}

struct mcubus_stats {
	uint32_t sent;    /* requests to others that got an answer */
	uint32_t served;  /* requests of others that this MCU has answered */
	uint32_t retries; /* attempts lost to other masters, or to a busy bus */
};

struct mcubus_info {
	uint8_t version; /* MCUBUS_PROTOCOL_VERSION of that MCU */
	uint8_t msg_max; /* its MCUBUS_MSG_MAX */
};

/* Name (devicetree node), I2C address and bus of this MCU */
const char *mcubus_name(void);
uint8_t mcubus_addr(void);
const struct device *mcubus_i2c(void);

void mcubus_get_stats(struct mcubus_stats *stats);

/*
 * Send request `cmd`, `req` to the MCU at `addr` and read `rsp_len` bytes of the
 * payload of its response into `rsp` (bytes the service did not write read as
 * 0xff); `addr` may be this MCU. An arbitration loss or a busy bus is retried
 * with a backoff, for up to CONFIG_MCUBUS_RETRY_MS. Returns 0; the -errno of the
 * transfer: -EIO nobody answers at `addr`, -EAGAIN / -EBUSY the bus stayed
 * taken; or the -errno the service answered.
 *
 * A request runs at most once on the target, but its controller may not learn
 * that it did: if the read of the response fails, the result is an error, and
 * the request may have run or not.
 */
int mcubus_request(uint8_t addr, uint8_t cmd, const void *req, size_t req_len, void *rsp,
		   size_t rsp_len);

/*
 * Ask the MCU at `addr` for its protocol version and MCUBUS_MSG_MAX. Returns 0,
 * -EPROTO if they differ from this MCU's (`info` is filled in anyway), or the
 * -errno of the request. With CONFIG_MCUBUS_SHELL: "mcubus ping <addr>".
 */
int mcubus_ping(uint8_t addr, struct mcubus_info *info);

#endif /* MCUBUS_H_ */
