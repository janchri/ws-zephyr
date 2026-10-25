/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Requests between the MCUs on a multi-master I2C bus of equal peers.
 *
 * This MCU is the devicetree node /chosen zephyr,mcubus-self: a
 * "zephyr,mcubus-node" child of its I2C bus, at its address. From boot on it
 * serves requests at that address, and it sends requests to the other MCUs
 * (mcubus_request()). The bytes go over the multi-master I2C link
 * (<drivers/i2c/i2c_mm.h>), one transfer per request:
 *
 *   request:   cmd, src, data
 *   response:  status, payload    (status negative is -errno, then no payload)
 *
 * `src` is the address of the sender: an I2C target cannot see who the
 * controller is.
 *
 * The cmd picks a service (MCUBUS_SERVICE_DEFINE); a cmd that no service has
 * gets -ENOTSUP. The target runs the service in its interrupt, when the read
 * begins, so a service must be short and only copy memory. A request of the MCU
 * to itself does not go over the bus: the service runs in the calling thread.
 *
 * Controllers that start at the same moment arbitrate, and the lowest frame wins:
 * address byte, then cmd. A lower cmd has the higher priority. Every service has
 * a cmd of its own, defined in its own header:
 *
 *   0x00        ping       <mcubus/ping.h>
 *   0x01        settings   <mcubus/settings.h>
 *   0x10        UART       drivers/serial/uart_mcubus.c
 *   0x80..0xff  for applications
 */

#ifndef MCUBUS_H_
#define MCUBUS_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/iterable_sections.h>

#include <drivers/i2c/i2c_mm.h>

/* Version of the requests; every MCU on a bus needs the same */
#define MCUBUS_PROTOCOL_VERSION 1

/* Longest request (cmd and src included) and longest response (status included) */
#define MCUBUS_MSG_MAX I2C_MM_MSG_MAX

/* Longest data of a request, and longest payload of a response */
#define MCUBUS_DATA_MAX    (MCUBUS_MSG_MAX - 2)
#define MCUBUS_PAYLOAD_MAX (MCUBUS_MSG_MAX - 1)

/* This MCU in the devicetree */
#define MCUBUS_SELF DT_CHOSEN(zephyr_mcubus_self)

/* A request, as a service gets it */
struct mcubus_req {
	uint8_t src; /* address of the MCU that sent it */
	const uint8_t *data;
	size_t len;
};

struct mcubus_service {
	uint8_t cmd;
	/*
	 * Write the payload of the response to `rsp` (room for MCUBUS_PAYLOAD_MAX
	 * bytes) and return its length, or return -errno. The status byte in front
	 * of the payload is mcubus's: 0, or that -errno alone.
	 */
	int (*handle)(const struct mcubus_req *req, uint8_t *rsp);
};

#define MCUBUS_SERVICE_DEFINE(name, _cmd, _handle)                                                 \
	static const STRUCT_SECTION_ITERABLE(mcubus_service, name) = {                             \
		.cmd = _cmd,                                                                       \
		.handle = _handle,                                                                 \
	}

/* Name (devicetree node) and I2C address of this MCU, and its link to the bus */
const char *mcubus_name(void);
uint8_t mcubus_addr(void);
const struct i2c_mm *mcubus_link(void);

/*
 * Send request `cmd`, `data` to the MCU at `addr` and read `rsp_len` bytes of the
 * payload of its response into `rsp` (bytes the service did not write read as
 * 0xff); `addr` may be this MCU. Returns 0; the -errno of the transfer
 * (i2c_mm_transfer()): -EIO nobody answers at `addr`, -EAGAIN / -EBUSY the bus
 * stayed taken; or the -errno the service answered.
 *
 * A request runs at most once on the target, but its controller may not learn
 * that it did: if the read of the response fails, the result is an error, and
 * the request may have run or not.
 */
int mcubus_request(uint8_t addr, uint8_t cmd, const void *data, size_t len, void *rsp,
		   size_t rsp_len);

#ifdef CONFIG_MCUBUS_SHELL
struct shell;

/*
 * For the shell commands of services, under "mcubus" (SHELL_SUBCMD_ADD((mcubus),
 * ...)): parse an I2C address. 0, or -EINVAL with an error printed.
 */
int mcubus_shell_addr(const struct shell *sh, const char *arg, uint8_t *addr);
#endif

#endif /* MCUBUS_H_ */
