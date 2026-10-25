/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>

#include <mcubus/mcubus.h>
#include <mcubus/settings.h>

BUILD_ASSERT(1 + MCUBUS_SETTINGS_KEY_MAX + MCUBUS_SETTINGS_VALUE_MAX <= MCUBUS_DATA_MAX,
	     "a SET must fit CONFIG_I2C_MM_MSG_MAX");
BUILD_ASSERT(1 + MCUBUS_SETTINGS_VALUE_MAX <= MCUBUS_PAYLOAD_MAX,
	     "a GET response must fit CONFIG_I2C_MM_MSG_MAX");

/* ---- the service, for the others or for this MCU ------------------------- */

static void save(struct k_work *work)
{
	ARG_UNUSED(work);

	settings_save();
}

static K_WORK_DEFINE(save_work, save);

/* The payload of the response: len, value */
static int get(const char *key, uint8_t *rsp)
{
	int ret = settings_runtime_get(key, &rsp[1], MCUBUS_SETTINGS_VALUE_MAX);

	if (ret < 0) {
		return ret;
	}
	rsp[0] = ret;
	return 1 + ret;
}

static int set(const char *key, const uint8_t *value, size_t len)
{
	int ret = settings_runtime_set(key, value, len);

	if (ret < 0) {
		return ret;
	}
	k_work_submit(&save_work); /* not here: flash is slow, this may be an interrupt */
	return 0;
}

/* The data is op, key, NUL; and the value for a SET */
static int handle(const struct mcubus_req *req, uint8_t *rsp)
{
	const char *key = (const char *)&req->data[1];
	const uint8_t *nul = req->len > 1 ? memchr(key, '\0', req->len - 1) : NULL;
	const uint8_t *end = &req->data[req->len];

	if (nul == NULL || nul == (const uint8_t *)key) {
		return -EINVAL;
	}
	switch (req->data[0]) {
	case MCUBUS_SETTINGS_GET:
		return get(key, rsp);
	case MCUBUS_SETTINGS_SET:
		return set(key, nul + 1, end - (nul + 1));
	default:
		return -ENOTSUP;
	}
}

MCUBUS_SERVICE_DEFINE(mcubus_settings_service, MCUBUS_SETTINGS_CMD, handle);

/* ---- requests -------------------------------------------------------------- */

/* op, key, NUL into `req`: returns its length, or -EINVAL for a key that is too long */
static int put_key(uint8_t *req, uint8_t op, const char *key)
{
	size_t key_len = strlen(key) + 1;

	if (key_len > MCUBUS_SETTINGS_KEY_MAX) {
		return -EINVAL;
	}
	req[0] = op;
	memcpy(&req[1], key, key_len);
	return 1 + key_len;
}

int mcubus_settings_get(uint8_t addr, const char *key, void *data, uint8_t len)
{
	uint8_t req[1 + MCUBUS_SETTINGS_KEY_MAX];
	uint8_t rsp[1 + MCUBUS_SETTINGS_VALUE_MAX];
	int req_len = put_key(req, MCUBUS_SETTINGS_GET, key);
	int ret;

	if (req_len < 0) {
		return req_len;
	}
	len = MIN(len, MCUBUS_SETTINGS_VALUE_MAX);
	ret = mcubus_request(addr, MCUBUS_SETTINGS_CMD, req, req_len, rsp, 1 + len);
	if (ret) {
		return ret;
	}
	memcpy(data, &rsp[1], MIN(len, rsp[0]));
	return rsp[0];
}

int mcubus_settings_set(uint8_t addr, const char *key, const void *data, uint8_t len)
{
	uint8_t req[1 + MCUBUS_SETTINGS_KEY_MAX + MCUBUS_SETTINGS_VALUE_MAX];
	int req_len = put_key(req, MCUBUS_SETTINGS_SET, key);

	if (req_len < 0 || len > MCUBUS_SETTINGS_VALUE_MAX) {
		return -EINVAL;
	}
	memcpy(&req[req_len], data, len);
	return mcubus_request(addr, MCUBUS_SETTINGS_CMD, req, req_len + len, NULL, 0);
}

/* ---- shell ------------------------------------------------------------------- */

#ifdef CONFIG_MCUBUS_SHELL

/* mcubus get <addr> <key> */
static int cmd_get(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t val[MCUBUS_SETTINGS_VALUE_MAX];
	bool text = true;
	uint8_t addr;
	int ret;
	int len;

	if (mcubus_shell_addr(sh, argv[1], &addr)) {
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

	if (mcubus_shell_addr(sh, argv[1], &addr)) {
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

SHELL_SUBCMD_ADD((mcubus), get, NULL, "<addr> <key>: get a setting of the MCU at <addr>", cmd_get,
		 3, 0);
SHELL_SUBCMD_ADD((mcubus), set, NULL, "<addr> <key> <text>: set a setting of the MCU at <addr>",
		 cmd_set, 4, 0);

#endif /* CONFIG_MCUBUS_SHELL */
