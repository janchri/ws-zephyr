/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "controller.h"
#include "http.h"
#include "storage.h"

LOG_MODULE_REGISTER(ww, CONFIG_WW_LOG_LEVEL);

int main(void)
{
	int ret;

	LOG_INF("Hello WirbelWind");

	ret = settings_subsys_init();
	if (ret) {
		LOG_ERR("Settings init failed: %d", ret);
		return ret;
	}

	ret = ww_storage_init();
	if (ret) {
		LOG_ERR("No SD card, giving up");
		return ret;
	}

	ret = ww_controller_init();
	if (ret) {
		return ret;
	}

	if (IS_ENABLED(CONFIG_WW_HTTP)) {
		ret = ww_http_init();
	}

	return ret;
}
