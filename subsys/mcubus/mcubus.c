/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <mcubus/mcubus.h>

LOG_MODULE_REGISTER(mcubus, CONFIG_MCUBUS_LOG_LEVEL);

BUILD_ASSERT(DT_NODE_EXISTS(MCUBUS_SELF), "mcubus needs /chosen zephyr,mcubus-self");

static struct i2c_mm link;

const char *mcubus_name(void)
{
	return DT_NODE_FULL_NAME(MCUBUS_SELF);
}

uint8_t mcubus_addr(void)
{
	return DT_REG_ADDR(MCUBUS_SELF);
}

const struct i2c_mm *mcubus_link(void)
{
	return &link;
}

static size_t dispatch(struct i2c_mm *mm, const uint8_t *req, size_t len, uint8_t *rsp,
		       size_t rsp_max)
{
	int ret = -EINVAL;

	ARG_UNUSED(mm);
	__ASSERT_NO_MSG(rsp_max == MCUBUS_MSG_MAX);

	if (len >= MCUBUS_HDR_LEN) {
		const struct mcubus_req r = {
			.src = req[1],
			.data = &req[MCUBUS_HDR_LEN],
			.len = len - MCUBUS_HDR_LEN,
		};

		ret = -ENOTSUP;
		STRUCT_SECTION_FOREACH(mcubus_service, s) {
			if (s->cmd == req[0]) {
				ret = s->handle(&r, &rsp[1]);
				break;
			}
		}
	}
	/* errno without its sign: Zephyr's errno values go past 127 (ENOTSUP is 134) */
	rsp[0] = ret < 0 ? MIN(-ret, UINT8_MAX) : 0;
	return 1 + MAX(ret, 0);
}

int mcubus_request(uint8_t addr, uint8_t cmd, const void *data, size_t len, void *rsp,
		   size_t rsp_len)
{
	uint8_t req[MCUBUS_MSG_MAX];
	uint8_t in[MCUBUS_MSG_MAX];
	int ret;

	if (len > MCUBUS_DATA_MAX || rsp_len > MCUBUS_PAYLOAD_MAX) {
		return -EINVAL;
	}
	req[0] = cmd;
	req[1] = mcubus_addr();
	req[2] = 0;
	if (len > 0) {
		memcpy(&req[MCUBUS_HDR_LEN], data, len);
	}

	ret = i2c_mm_transfer(&link, addr, req, MCUBUS_HDR_LEN + len, in, 1 + rsp_len);
	if (ret) {
		return ret;
	}
	if (in[0] != 0) {
		return -in[0];
	}
	if (rsp_len > 0) {
		memcpy(rsp, &in[1], rsp_len);
	}
	return 0;
}

static bool cmds_unique(void)
{
	bool unique = true;

	STRUCT_SECTION_FOREACH(mcubus_service, a) {
		STRUCT_SECTION_FOREACH(mcubus_service, b) {
			if (a < b && a->cmd == b->cmd) {
				LOG_ERR("two services with cmd 0x%02x", a->cmd);
				unique = false;
			}
		}
	}
	return unique;
}

static int mcubus_init(void)
{
	if (!cmds_unique()) {
		return -EEXIST;
	}
	return i2c_mm_init(&link, DEVICE_DT_GET(DT_BUS(MCUBUS_SELF)), mcubus_addr(), dispatch);
}

SYS_INIT(mcubus_init, POST_KERNEL, CONFIG_MCUBUS_INIT_PRIORITY);

#ifdef CONFIG_MCUBUS_SHELL

int mcubus_shell_addr(const struct shell *sh, const char *arg, uint8_t *addr)
{
	int err = 0;
	unsigned long v = shell_strtoul(arg, 0, &err);

	if (err || v > 0x7f) {
		shell_error(sh, "not an I2C address: %s", arg);
		return -EINVAL;
	}
	*addr = v;
	return 0;
}

static int cmd_stats(const struct shell *sh, size_t argc, char **argv)
{
	struct i2c_mm_stats s;

	i2c_mm_get_stats(&link, &s);
	shell_print(sh, "%s at 0x%02x: sent %u, served %u, retries %u", mcubus_name(),
		    mcubus_addr(), s.sent, s.served, s.retries);
	return 0;
}

SHELL_SUBCMD_SET_CREATE(mcubus_cmds, (mcubus));
SHELL_SUBCMD_ADD((mcubus), stats, NULL, "requests of this MCU", cmd_stats, 1, 0);
SHELL_CMD_REGISTER(mcubus, &mcubus_cmds, "The MCUs on the bus", NULL);

#endif /* CONFIG_MCUBUS_SHELL */
