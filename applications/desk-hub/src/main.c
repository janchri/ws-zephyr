/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_manager.h"
#include "app_ui.h"
#include "display_backend.h"

int main(void)
{
	int ret = desk_hub_display_backend_init();

	if (ret < 0) {
		return ret;
	}
	ret = app_ui_init();
	if (ret < 0) {
		return ret;
	}
	return desk_hub_app_manager_run();
}
