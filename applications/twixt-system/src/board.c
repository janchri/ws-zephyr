/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * A board on the bus: its shell is on a UART over the bus to the gateway
 * (board.overlay), and it serves one read-only setting, mcu/name, that the
 * others can get ("twixt get <addr> mcu/name"). Its settings are in NVS on the
 * settings partition; littlefs on the storage partition is mounted at /lfs.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell_uart.h>

#include <twixt/twixt.h>

LOG_MODULE_REGISTER(board, LOG_LEVEL_INF);

/* Runs in the I2C target interrupt too: a constant needs no lock */
static int mcu_get(const char *key, char *val, int val_len_max)
{
	int len = strlen(twixt_name());

	if (!settings_name_steq(key, "name", NULL)) {
		return -ENOENT;
	}
	if (len > val_len_max) {
		return -ENOMEM;
	}
	memcpy(val, twixt_name(), len);
	return len;
}

static int mcu_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	return settings_name_steq(key, "name", NULL) ? -EACCES : -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(mcu, "mcu", mcu_get, mcu_set, NULL, NULL);

int main(void)
{
	static char prompt[32];
	int ret;

	ret = settings_subsys_init();
	if (ret) {
		LOG_ERR("settings: %d", ret);
	}

	snprintf(prompt, sizeof(prompt), "%s:~$ ", twixt_name());
	shell_prompt_change(shell_backend_uart_get_ptr(), prompt);

	LOG_INF("%s at 0x%02x, shell on the bus", twixt_name(), twixt_addr());
	return 0;
}
