/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_runtime.h"
#include "app_ui.h"
#include <errno.h>

int app_runtime_next_event(enum desk_hub_event *event)
{
	app_ui_process();
	return desk_hub_event_get(event, K_MSEC(10));
}

int app_runtime_run(const struct desk_hub_app *app)
{
	if (app == NULL || app->on_run == NULL) {
		return -EINVAL;
	}
	app_ui_clear(false);
	int ret = app->on_entry != NULL ? app->on_entry() : 0;

	if (ret == 0) {
		ret = app->on_run();
	}
	if (app->on_exit != NULL) {
		app->on_exit();
	}
	app_ui_clear(false);
	return ret;
}
