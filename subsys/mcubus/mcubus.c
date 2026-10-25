/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <mcubus/mcubus.h>
#include <mcubus/settings.h>

LOG_MODULE_REGISTER(mcubus, CONFIG_MCUBUS_LOG_LEVEL);

BUILD_ASSERT(DT_NODE_EXISTS(MCUBUS_SELF), "mcubus needs /chosen zephyr,mcubus-self");

static const struct device *const i2c = DEVICE_DT_GET(DT_BUS(MCUBUS_SELF));

static struct {
	struct mcubus_stats stats;

	/* the request the target interrupt is in */
	struct i2c_target_config cfg;
	uint8_t rx[MCUBUS_MSG_MAX];
	uint8_t rx_len;
	uint8_t tx[MCUBUS_MSG_MAX];
	uint8_t tx_len;
	uint8_t tx_pos;
} bus;

const char *mcubus_name(void)
{
	return DT_NODE_FULL_NAME(MCUBUS_SELF);
}

uint8_t mcubus_addr(void)
{
	return DT_REG_ADDR(MCUBUS_SELF);
}

const struct device *mcubus_i2c(void)
{
	return i2c;
}

void mcubus_get_stats(struct mcubus_stats *stats)
{
	*stats = bus.stats;
}

/*
 * Run request `req` (cmd first) for the others or for this MCU: the status goes to
 * rsp[0], the service's payload after it. Returns the response length.
 */
static size_t dispatch(const uint8_t *req, size_t len, uint8_t *rsp)
{
	int ret = -EINVAL;

	if (len > 0) {
		ret = -ENOTSUP;
		STRUCT_SECTION_FOREACH(mcubus_service, s) {
			if (s->cmd == req[0]) {
				ret = s->handle(&req[1], len - 1, &rsp[1]);
				break;
			}
		}
	}
	rsp[0] = MIN(ret, 0);
	return 1 + MAX(ret, 0);
}

/* ---- target side: runs in the I2C interrupt ------------------------------ */

static uint8_t next_byte(void)
{
	return bus.tx_pos < bus.tx_len ? bus.tx[bus.tx_pos++] : 0xff;
}

/*
 * At the start of a write, and at STOP: a request that was written but never read
 * (its controller gave up) is not run, then or later
 */
static int forget_request(struct i2c_target_config *cfg)
{
	bus.rx_len = 0;
	return 0;
}

static int write_received(struct i2c_target_config *cfg, uint8_t val)
{
	if (bus.rx_len >= sizeof(bus.rx)) {
		return -ENOMEM; /* NACK: request too long */
	}
	bus.rx[bus.rx_len++] = val;
	return 0;
}

/*
 * The read of the response begins: run the request that was written. Then forget
 * it, so that a read alone (a bus scan, a controller that lost its place) gets
 * -EINVAL instead of running the last request again.
 */
static int read_requested(struct i2c_target_config *cfg, uint8_t *val)
{
	bus.tx_len = dispatch(bus.rx, bus.rx_len, bus.tx);
	bus.tx_pos = 0;
	bus.rx_len = 0;
	bus.stats.served++;
	*val = next_byte();
	return 0;
}

static int read_processed(struct i2c_target_config *cfg, uint8_t *val)
{
	*val = next_byte();
	return 0;
}

static const struct i2c_target_callbacks target_cb = {
	.write_requested = forget_request,
	.write_received = write_received,
	.read_requested = read_requested,
	.read_processed = read_processed,
	.stop = forget_request,
};

static int mcubus_init(void)
{
	int ret;

	if (!device_is_ready(i2c)) {
		LOG_ERR("%s: I2C not ready", mcubus_name());
		return -ENODEV;
	}
	bus.cfg.address = mcubus_addr();
	bus.cfg.callbacks = &target_cb;
	ret = i2c_target_register(i2c, &bus.cfg);
	if (ret) {
		LOG_ERR("%s: cannot serve at 0x%02x: %d", mcubus_name(), mcubus_addr(), ret);
	}
	return ret;
}

SYS_INIT(mcubus_init, POST_KERNEL, CONFIG_MCUBUS_INIT_PRIORITY);

/* ---- ping: which protocol, which limits ----------------------------------- */

static int ping(const uint8_t *req, size_t len, uint8_t *rsp)
{
	rsp[0] = MCUBUS_PROTOCOL_VERSION;
	rsp[1] = MCUBUS_MSG_MAX;
	return 2;
}

MCUBUS_SERVICE_DEFINE(mcubus_ping_service, MCUBUS_CMD_PING, ping);

/* ---- controller side ------------------------------------------------------- */

static int transfer_retry(uint8_t addr, const uint8_t *req, size_t req_len, uint8_t *rsp,
			  size_t rsp_len)
{
	int64_t end = k_uptime_get() + CONFIG_MCUBUS_RETRY_MS;

	for (uint32_t i = 1;; i++) {
		int ret = i2c_write_read(i2c, addr, req, req_len, rsp, rsp_len);

		if (IS_ENABLED(CONFIG_MCUBUS_TARGET_REREGISTER)) {
			/* the driver left target mode for the transfer */
			(void)i2c_target_register(i2c, &bus.cfg);
		}
		if ((ret != -EAGAIN && ret != -EBUSY) || k_uptime_get() >= end) {
			return ret;
		}
		bus.stats.retries++;
		LOG_WRN("%s: request to 0x%02x attempt %u: %s, backing off", mcubus_name(), addr, i,
			ret == -EAGAIN ? "lost arbitration" : "bus busy");
		/* out of the winner's START window, then wait for its STOP */
		k_sleep(K_MSEC(2 * MIN(i, 4)));
	}
}

int mcubus_request(uint8_t addr, uint8_t cmd, const void *req, size_t req_len, void *rsp,
		   size_t rsp_len)
{
	uint8_t frame[MCUBUS_MSG_MAX];
	uint8_t in[MCUBUS_MSG_MAX];
	int ret = 0;

	if (1 + req_len > sizeof(frame) || 1 + rsp_len > sizeof(in)) {
		return -EINVAL;
	}
	frame[0] = cmd;
	memcpy(&frame[1], req, req_len);

	if (addr == mcubus_addr()) {
		size_t len = dispatch(frame, 1 + req_len, in);

		memset(&in[len], 0xff, sizeof(in) - len); /* as the bus reads what was not sent */
	} else {
		ret = transfer_retry(addr, frame, 1 + req_len, in, 1 + rsp_len);
		if (ret) {
			return ret;
		}
		bus.stats.sent++;
	}
	if ((int8_t)in[0] < 0) {
		return (int8_t)in[0];
	}
	memcpy(rsp, &in[1], rsp_len);
	return 0;
}

int mcubus_ping(uint8_t addr, struct mcubus_info *info)
{
	uint8_t rsp[2];
	int ret = mcubus_request(addr, MCUBUS_CMD_PING, NULL, 0, rsp, sizeof(rsp));

	if (ret) {
		return ret;
	}
	info->version = rsp[0];
	info->msg_max = rsp[1];
	return info->version == MCUBUS_PROTOCOL_VERSION && info->msg_max == MCUBUS_MSG_MAX
		       ? 0
		       : -EPROTO;
}

/* ---- shell ------------------------------------------------------------------- */

#ifdef CONFIG_MCUBUS_SHELL

static int parse_addr(const struct shell *sh, const char *arg, uint8_t *addr)
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

/* mcubus ping <addr> */
static int cmd_ping(const struct shell *sh, size_t argc, char **argv)
{
	struct mcubus_info info;
	uint8_t addr;
	int ret;

	if (parse_addr(sh, argv[1], &addr)) {
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

/* mcubus stats */
static int cmd_stats(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "%s at 0x%02x: sent %u, served %u, retries %u", mcubus_name(),
		    mcubus_addr(), bus.stats.sent, bus.stats.served, bus.stats.retries);
	return 0;
}

#ifdef CONFIG_MCUBUS_SETTINGS

/* mcubus get <addr> <key> */
static int cmd_get(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t val[MCUBUS_SETTINGS_VALUE_MAX];
	bool text = true;
	uint8_t addr;
	int ret;
	int len;

	if (parse_addr(sh, argv[1], &addr)) {
		return -EINVAL;
	}
	ret = mcubus_settings_get(addr, argv[2], val, sizeof(val));
	if (ret < 0) {
		shell_error(sh, "0x%02x %s: %d", addr, argv[2], ret);
		return ret;
	}
	len = MIN(ret, (int)sizeof(val)); /* a peer may have longer values */
	for (int i = 0; i < len; i++) {
		text = text && isprint(val[i]);
	}
	if (text && len == ret) {
		shell_print(sh, "0x%02x %s = \"%.*s\"", addr, argv[2], len, val);
	} else {
		shell_print(sh, "0x%02x %s = %d bytes", addr, argv[2], ret);
		shell_hexdump(sh, val, len);
	}
	return 0;
}

/* mcubus set <addr> <key> <text> */
static int cmd_set(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t addr;
	int ret;

	if (parse_addr(sh, argv[1], &addr)) {
		return -EINVAL;
	}
	ret = mcubus_settings_set(addr, argv[2], argv[3], strlen(argv[3]));
	if (ret < 0) {
		shell_error(sh, "0x%02x %s: %d", addr, argv[2], ret);
		return ret;
	}
	shell_print(sh, "0x%02x %s set", addr, argv[2]);
	return 0;
}

/* clang-format off */
#define SETTINGS_CMDS                                                                              \
	SHELL_CMD_ARG(get, NULL, "<addr> <key>: get a setting of the MCU at <addr>", cmd_get, 3,   \
		      0),                                                                          \
	SHELL_CMD_ARG(set, NULL, "<addr> <key> <text>: set a setting of the MCU at <addr>",       \
		      cmd_set, 4, 0),
#else
#define SETTINGS_CMDS
#endif /* CONFIG_MCUBUS_SETTINGS */

SHELL_STATIC_SUBCMD_SET_CREATE(mcubus_cmds,
	SHELL_CMD_ARG(ping, NULL, "<addr>: protocol and limits of the MCU at <addr>", cmd_ping,
		      2, 0),
	SHELL_CMD(stats, NULL, "requests of this MCU", cmd_stats),
	SETTINGS_CMDS
	SHELL_SUBCMD_SET_END);
/* clang-format on */

SHELL_CMD_REGISTER(mcubus, &mcubus_cmds, "The MCUs on the bus", NULL);

#endif /* CONFIG_MCUBUS_SHELL */
