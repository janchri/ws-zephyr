/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Requests between equal MCUs on a multi-master I2C bus.
 */

#ifndef MCUBUS_H_
#define MCUBUS_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/iterable_sections.h>

#include <drivers/i2c/i2c_mm.h>

/**
 * @defgroup mcubus mcubus
 * @{
 *
 * This MCU is /chosen zephyr,mcubus-self, a "zephyr,mcubus-node" on its I2C bus.
 * It serves requests at its address from boot on and sends requests to the
 * others, one link transfer each (@ref i2c_mm):
 *
 *     request:   cmd, src, flags, data
 *     response:  status, payload
 *
 * @c cmd selects the service, @c src is the sender's address, @c flags is
 * reserved: written as 0, ignored on receipt. @c status is 0 or an errno
 * without its sign; an error has no payload.
 *
 * The lowest frame wins arbitration, so a lower cmd has the higher priority.
 * Allocated cmds: 0x00 ping, 0x01 settings, 0x10 UART; 0x80..0xff are for
 * applications.
 */

/** Protocol version; every MCU on a bus needs the same */
#define MCUBUS_PROTOCOL_VERSION 1

/** Longest request and longest response, header and status included */
#define MCUBUS_MSG_MAX I2C_MM_MSG_MAX

/** Request header: cmd, src, flags */
#define MCUBUS_HDR_LEN 3

/** Longest data of a request */
#define MCUBUS_DATA_MAX (MCUBUS_MSG_MAX - MCUBUS_HDR_LEN)

/** Longest payload of a response */
#define MCUBUS_PAYLOAD_MAX (MCUBUS_MSG_MAX - 1)

/** Devicetree node of this MCU */
#define MCUBUS_SELF DT_CHOSEN(zephyr_mcubus_self)

/** @brief A request as a service gets it */
struct mcubus_req {
	/** Address of the sender */
	uint8_t src;
	/** Data after the header */
	const uint8_t *data;
	/** Length of @p data */
	size_t len;
};

/** @brief A service */
struct mcubus_service {
	/** cmd it serves */
	uint8_t cmd;
	/**
	 * @brief Answer a request.
	 *
	 * Runs in the I2C target interrupt, or in the sending thread for a request
	 * of this MCU to itself, possibly both at once: keep it short, and shared
	 * state under a spinlock. Must not return -EIO, -EAGAIN or -EBUSY, which
	 * mean "bus" to the caller.
	 *
	 * @param req Request.
	 * @param rsp Payload to write, room for MCUBUS_PAYLOAD_MAX bytes.
	 *
	 * @return Length of the payload, or -errno.
	 */
	int (*handle)(const struct mcubus_req *req, uint8_t *rsp);
};

/**
 * @brief Define a service.
 *
 * @param name Name of the definition.
 * @param _cmd cmd it serves; unique on the MCU, else mcubus does not start.
 * @param _handle Handler, see mcubus_service::handle.
 */
#define MCUBUS_SERVICE_DEFINE(name, _cmd, _handle)                                                 \
	static const STRUCT_SECTION_ITERABLE(mcubus_service, name) = {                             \
		.cmd = _cmd,                                                                       \
		.handle = _handle,                                                                 \
	}

/** @brief Name of this MCU. @return Its devicetree node name. */
const char *mcubus_name(void);

/** @brief I2C address of this MCU. @return Address. */
uint8_t mcubus_addr(void);

/** @brief Link of this MCU to the bus. @return Link. */
const struct i2c_mm *mcubus_link(void);

/**
 * @brief Send a request and read the payload of the response.
 *
 * Payload bytes the service did not write read as 0xff. A request to this MCU
 * runs the service directly. A request runs at most once, but if reading the
 * response fails, it may or may not have run.
 *
 * @param addr MCU to send to, may be this one.
 * @param cmd Service.
 * @param data Data of the request.
 * @param len Length of @p data, up to MCUBUS_DATA_MAX.
 * @param rsp Payload buffer.
 * @param rsp_len Payload bytes to read, up to MCUBUS_PAYLOAD_MAX.
 *
 * @retval 0 Done.
 * @retval -EIO Nobody answers at @p addr.
 * @retval -EAGAIN Lost arbitration until the deadline.
 * @retval -EBUSY Bus busy until the deadline.
 * @retval -ENOTSUP No service for @p cmd there.
 * @retval -errno Error of the service.
 */
int mcubus_request(uint8_t addr, uint8_t cmd, const void *data, size_t len, void *rsp,
		   size_t rsp_len);

#if defined(CONFIG_MCUBUS_SHELL) || defined(__DOXYGEN__)
struct shell;

/**
 * @brief Parse an I2C address, for shell commands of services.
 *
 * Services add their commands under "mcubus" with SHELL_SUBCMD_ADD((mcubus), ...).
 *
 * @param sh Shell, for the error.
 * @param arg Text to parse.
 * @param addr Address.
 *
 * @retval 0 Parsed.
 * @retval -EINVAL Not an address; the error is printed.
 */
int mcubus_shell_addr(const struct shell *sh, const char *arg, uint8_t *addr);
#endif

/** @} */

#endif /* MCUBUS_H_ */
