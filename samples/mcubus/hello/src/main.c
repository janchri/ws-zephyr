/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hello over the mcubus: one MCU of two. It serves an echo service of its own,
 * and calls the echo of the other MCU.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <mcubus/mcubus.h>
#include <mcubus/ping.h>

LOG_MODULE_REGISTER(hello, LOG_LEVEL_INF);

#define ECHO_CMD 0x80

/* Runs in the I2C target interrupt: short, and only copying */
static int echo(const struct mcubus_req *req, uint8_t *rsp)
{
	rsp[0] = req->src;
	memcpy(&rsp[1], req->data, req->len);
	return 1 + req->len;
}

MCUBUS_SERVICE_DEFINE(echo_service, ECHO_CMD, echo);

#define ADDR(node) DT_REG_ADDR(node),
static const uint8_t mcus[] = {DT_FOREACH_STATUS_OKAY(zephyr_mcubus_node, ADDR)};

static uint8_t other_mcu(void)
{
	return mcus[0] != mcubus_addr() ? mcus[0] : mcus[1];
}

/* The other MCU may still be booting: ping it until it answers */
static int wait_for(uint8_t addr)
{
	struct mcubus_info info;
	int ret;

	for (int i = 0; i < 50; i++) {
		ret = mcubus_ping(addr, &info);
		if (ret != -EIO) {
			break;
		}
		k_msleep(100);
	}
	if (ret == 0) {
		LOG_INF("0x%02x answers: protocol %u", addr, info.version);
	}
	return ret;
}

int main(void)
{
	uint8_t peer = other_mcu();
	char msg[32];
	uint8_t rsp[1 + sizeof(msg)] = {0};
	int len;
	int ret;

	BUILD_ASSERT(ARRAY_SIZE(mcus) == 2, "hello is for two MCUs");

	LOG_INF("%s at 0x%02x", mcubus_name(), mcubus_addr());

	ret = wait_for(peer);
	if (ret) {
		LOG_ERR("no answer from 0x%02x: %d", peer, ret);
		return 0;
	}

	len = snprintf(msg, sizeof(msg), "hello from %s", mcubus_name());
	ret = mcubus_request(peer, ECHO_CMD, msg, len, rsp, 1 + len);
	if (ret) {
		LOG_ERR("echo of 0x%02x: %d", peer, ret);
		return 0;
	}
	LOG_INF("0x%02x echoed \"%.*s\", from 0x%02x", peer, len, &rsp[1], rsp[0]);
	return 0;
}
