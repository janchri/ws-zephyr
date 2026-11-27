/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "calendar.h"

#include "app_registry.h"
#include "app_runtime.h"
#include "calendar_model.h"
#include "calendar_view.h"
#include "event.h"

#include <errno.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/printk.h>

DESK_HUB_APP_REGISTER(app_b_calendar, "CALENDAR", desk_hub_calendar_run,
		      .on_entry = desk_hub_calendar_view_init,
		      .on_exit = desk_hub_calendar_view_deinit);

static int refresh_calendar(int month_offset)
{
	struct timespec now;
	struct tm *utc;
	struct desk_hub_calendar_data data;

	if (sys_clock_gettime(SYS_CLOCK_REALTIME, &now) < 0) {
		return -EIO;
	}
	utc = gmtime(&now.tv_sec);
	if (utc == NULL) {
		return -EIO;
	}
	desk_hub_calendar_model_build(utc, month_offset, &data);
	return desk_hub_calendar_view_render(&data);
}

int desk_hub_calendar_run(void)
{
	int month_offset = 0;
	int64_t redraw_at = 0;
	bool redraw = true;

	while (true) {
		enum desk_hub_event event;
		int ret;

		if (redraw || k_uptime_get() >= redraw_at) {
			ret = refresh_calendar(month_offset);
			if (ret < 0) {
				return ret;
			}
			redraw = false;
			redraw_at = k_uptime_get() + 1000;
		}
		ret = app_runtime_next_event(&event);
		if (ret < 0) {
			continue;
		}
		if (event == DESK_HUB_EVENT_LONG_LEFT) {
			return 0;
		}
		if (event == DESK_HUB_EVENT_SHORT_LEFT) {
			month_offset--;
			redraw = true;
		} else if (event == DESK_HUB_EVENT_SHORT_RIGHT) {
			month_offset++;
			redraw = true;
		}
	}
}
