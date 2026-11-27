/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_manager.h"

#include "app_selector_model.h"
#include "app_selector_view.h"
#include "clock.h"
#include "event.h"

#include "app_registry.h"
#include "app_runtime.h"
#include <errno.h>
#include <zephyr/sys/printk.h>

static size_t selected;

static int selector_on_entry(void)
{
	desk_hub_clock_init();
	return desk_hub_app_count() == 0 ? -ENODEV : 0;
}

static int selector_on_run(void)
{
	const size_t app_count = desk_hub_app_count();
	size_t drawn = SIZE_MAX;
	int ret;

	if (app_count == 0) {
		return -ENODEV;
	}

	while (true) {
		enum desk_hub_event event;

		if (drawn != selected) {
			const char *names[APP_SELECTOR_PAGE_SIZE];
			const size_t first =
				selected / APP_SELECTOR_PAGE_SIZE * APP_SELECTOR_PAGE_SIZE;
			const size_t count = MIN(app_count - first, APP_SELECTOR_PAGE_SIZE);

			for (size_t i = 0; i < count; i++) {
				names[i] = desk_hub_app_at(first + i)->name;
			}
			ret = desk_hub_app_selector_view_render(names, count, selected - first);
			if (ret < 0) {
				return ret;
			}
			drawn = selected;
		}
		ret = app_runtime_next_event(&event);
		if (ret < 0) {
			continue;
		}
		if (event == DESK_HUB_EVENT_SHORT_LEFT) {
			selected = desk_hub_app_selector_move(selected, -1, app_count);
			drawn = SIZE_MAX;
		} else if (event == DESK_HUB_EVENT_SHORT_RIGHT) {
			selected = desk_hub_app_selector_move(selected, 1, app_count);
			drawn = SIZE_MAX;
		} else if (event == DESK_HUB_EVENT_LONG_RIGHT) {
			return 0;
		}
	}
}

int desk_hub_app_manager_run(void)
{
	const struct desk_hub_app selector = {
		.name = "APPS",
		.on_entry = selector_on_entry,
		.on_run = selector_on_run,
	};

	while (true) {
		int ret = app_runtime_run(&selector);

		if (ret < 0) {
			return ret;
		}
		const struct desk_hub_app *app = desk_hub_app_at(selected);

		ret = app_runtime_run(app);
		if (ret < 0) {
			printk("App %s stopped with error %d\n", app->name, ret);
		}
	}
}
