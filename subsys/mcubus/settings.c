/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <mcubus/mcubus.h>
#include <mcubus/settings.h>

BUILD_ASSERT(1 + MCUBUS_SETTINGS_KEY_MAX + MCUBUS_SETTINGS_VALUE_MAX <= MCUBUS_MSG_MAX,
	     "a SET must fit CONFIG_MCUBUS_MSG_MAX");
BUILD_ASSERT(2 + MCUBUS_SETTINGS_VALUE_MAX <= MCUBUS_MSG_MAX,
	     "a GET response must fit CONFIG_MCUBUS_MSG_MAX");

/* ---- the service, for the others or for this MCU ------------------------- */

static void save(struct k_work *work)
{
	ARG_UNUSED(work);

	settings_save();
}

static K_WORK_DEFINE(save_work, save);

/* `req` is key, NUL; and the value for a SET. NULL if there is no NUL. */
static const char *value_of(const uint8_t *req, size_t len)
{
	const uint8_t *nul = memchr(req, '\0', len);

	return nul != NULL && nul > req ? (const char *)nul + 1 : NULL;
}

/* The payload of the response: len, value */
static int get(const uint8_t *req, size_t len, uint8_t *rsp)
{
	int ret;

	if (value_of(req, len) == NULL) {
		return -EINVAL;
	}
	ret = settings_runtime_get((const char *)req, &rsp[1], MCUBUS_SETTINGS_VALUE_MAX);
	if (ret < 0) {
		return ret;
	}
	rsp[0] = ret;
	return 1 + ret;
}

static int set(const uint8_t *req, size_t len, uint8_t *rsp)
{
	const char *value = value_of(req, len);
	int ret;

	if (value == NULL) {
		return -EINVAL;
	}
	ret = settings_runtime_set((const char *)req, value, (const char *)&req[len] - value);
	if (ret < 0) {
		return ret;
	}
	k_work_submit(&save_work); /* not here: flash is slow, this may be an interrupt */
	return 0;
}

MCUBUS_SERVICE_DEFINE(mcubus_settings_get_service, MCUBUS_CMD_SETTINGS_GET, get);
MCUBUS_SERVICE_DEFINE(mcubus_settings_set_service, MCUBUS_CMD_SETTINGS_SET, set);

/* ---- requests -------------------------------------------------------------- */

int mcubus_settings_get(uint8_t addr, const char *key, void *data, uint8_t len)
{
	uint8_t rsp[1 + MCUBUS_SETTINGS_VALUE_MAX];
	size_t key_len = strlen(key) + 1;
	int ret;

	if (key_len > MCUBUS_SETTINGS_KEY_MAX) {
		return -EINVAL;
	}
	len = MIN(len, MCUBUS_SETTINGS_VALUE_MAX);
	ret = mcubus_request(addr, MCUBUS_CMD_SETTINGS_GET, key, key_len, rsp, 1 + len);
	if (ret) {
		return ret;
	}
	memcpy(data, &rsp[1], MIN(len, rsp[0]));
	return rsp[0];
}

int mcubus_settings_set(uint8_t addr, const char *key, const void *data, uint8_t len)
{
	uint8_t req[MCUBUS_SETTINGS_KEY_MAX + MCUBUS_SETTINGS_VALUE_MAX];
	size_t key_len = strlen(key) + 1;

	if (key_len > MCUBUS_SETTINGS_KEY_MAX || len > MCUBUS_SETTINGS_VALUE_MAX) {
		return -EINVAL;
	}
	memcpy(req, key, key_len);
	memcpy(&req[key_len], data, len);
	return mcubus_request(addr, MCUBUS_CMD_SETTINGS_SET, req, key_len + len, NULL, 0);
}
