/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/shell/shell.h>

#include <mcubus/mcubus.h>
#include <mcubus/ping.h>

static int ping(const struct mcubus_req *req, uint8_t *rsp)
{
	ARG_UNUSED(req);

	rsp[0] = MCUBUS_PROTOCOL_VERSION;
	rsp[1] = MCUBUS_MSG_MAX;
	return 2;
}

MCUBUS_SERVICE_DEFINE(mcubus_ping_service, MCUBUS_PING_CMD, ping);

int mcubus_ping(uint8_t addr, struct mcubus_info *info)
{
	uint8_t rsp[2];
	int ret = mcubus_request(addr, MCUBUS_PING_CMD, NULL, 0, rsp, sizeof(rsp));

	if (ret) {
		return ret;
	}
	info->version = rsp[0];
	info->msg_max = rsp[1];
	return info->version == MCUBUS_PROTOCOL_VERSION && info->msg_max == MCUBUS_MSG_MAX
		       ? 0
		       : -EPROTO;
}

#ifdef CONFIG_MCUBUS_SHELL

static int cmd_ping(const struct shell *sh, size_t argc, char **argv)
{
	struct mcubus_info info;
	uint8_t addr;
	int ret;

	if (mcubus_shell_addr(sh, argv[1], &addr)) {
		return -EINVAL;
	}
	ret = mcubus_ping(addr, &info);
	if (ret == 0 || ret == -EPROTO) {
		shell_print(sh, "0x%02x: protocol %u, messages up to %u bytes%s", addr,
			    info.version, info.msg_max, ret ? ": does not match this MCU" : "");
	} else {
		shell_error(sh, "0x%02x: %d", addr, ret);
	}
	return ret;
}

SHELL_SUBCMD_ADD((mcubus), ping, NULL, "<addr>: protocol and limits of the MCU at <addr>", cmd_ping,
		 2, 0);

#endif /* CONFIG_MCUBUS_SHELL */
