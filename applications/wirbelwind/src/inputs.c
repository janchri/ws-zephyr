/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hardware controls -> controller events, via the input subsystem:
 * the button (gpio-keys) and the volume potentiometer (analog-axis).
 */

#include <stdlib.h>

#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "events.h"

LOG_MODULE_DECLARE(ww, CONFIG_WW_LOG_LEVEL);

#define POT_NODE  DT_NODELABEL(volume_pot)
#define POT_RANGE DT_PROP(DT_CHILD(POT_NODE, axis), out_max)

/*
 * The button (gpio-keys, any key code): short, long and very long presses,
 * measured at release.
 */

static void button_cb(struct input_event *evt, void *user_data)
{
	static int64_t pressed_at;
	struct ww_event ev = {.type = WW_EV_BUTTON};
	int64_t duration;

	if (evt->type != INPUT_EV_KEY) {
		return;
	}
	if (evt->value) {
		pressed_at = k_uptime_get();
		return;
	}

	duration = k_uptime_get() - pressed_at;
	if (duration >= CONFIG_WW_BUTTON_LONGLONG_MS) {
		ev.press = WW_PRESS_LONGLONG;
	} else if (duration >= CONFIG_WW_BUTTON_LONG_MS) {
		ev.press = WW_PRESS_LONG;
	} else {
		ev.press = WW_PRESS_SHORT;
	}
	LOG_DBG("Button released after %lld ms", duration);
	ww_event_post(&ev);
}

INPUT_CALLBACK_DEFINE(NULL, button_cb, NULL);

/*
 * The volume potentiometer (analog-axis, 0..out-max). With some hysteresis
 * (half a step + 1 %), so a pot resting on a step boundary does not flicker.
 */

#if DT_NODE_EXISTS(POT_NODE)
static void pot_cb(struct input_event *evt, void *user_data)
{
	static int cur = -1;
	/* one volume step is POT_RANGE units */
	int pos = evt->value * CONFIG_WW_VOLUME_MAX;
	int center = cur * POT_RANGE;

	if (evt->type != INPUT_EV_ABS) {
		return;
	}
	if (cur >= 0 && abs(pos - center) <= POT_RANGE / 2 + POT_RANGE * CONFIG_WW_VOLUME_MAX / 100) {
		return;
	}

	cur = DIV_ROUND_CLOSEST(pos, POT_RANGE);
	ww_event_post_cmd(WW_CMD_VOLUME, cur);
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(POT_NODE), pot_cb, NULL);
#endif
