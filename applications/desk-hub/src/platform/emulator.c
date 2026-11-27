/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_ui.h"

#include "event.h"
#include "emulator_window.h"

#include <stdint.h>
#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#include <lvgl.h>

#define EMULATOR_TOOLBAR_HEIGHT 48
#define EMULATOR_INIT_PRIORITY  91

/* The SDL device and LVGL must exist before we create controls. */
BUILD_ASSERT(CONFIG_LV_Z_INIT_PRIORITY < EMULATOR_INIT_PRIORITY,
	     "Emulator controls must initialize after LVGL");

static void button_event(lv_event_t *event)
{
	enum desk_hub_button button =
		(enum desk_hub_button)(uintptr_t)lv_event_get_user_data(event);

	switch (lv_event_get_code(event)) {
	case LV_EVENT_PRESSED:
		desk_hub_button_state(button, true);
		break;
	case LV_EVENT_RELEASED:
	case LV_EVENT_PRESS_LOST:
		desk_hub_button_state(button, false);
		break;
	default:
		break;
	}
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, enum desk_hub_button button)
{
	lv_obj_t *object = lv_button_create(parent);
	lv_obj_set_size(object, 80, 30);
	lv_obj_set_style_radius(object, 3, 0);
	lv_obj_set_style_bg_color(object, lv_color_white(), 0);
	lv_obj_set_style_bg_color(object, lv_color_black(), LV_STATE_PRESSED);
	lv_obj_set_style_border_color(object, lv_color_white(), 0);
	lv_obj_set_style_border_width(object, 1, 0);
	lv_obj_add_event_cb(object, button_event, LV_EVENT_ALL, (void *)(uintptr_t)button);

	lv_obj_t *label = lv_label_create(object);
	lv_label_set_text(label, text);
	lv_obj_set_style_text_color(label, lv_color_black(), 0);
	lv_obj_set_style_text_color(label, lv_color_white(), LV_STATE_PRESSED);
	lv_obj_center(label);

	return object;
}

static void refresh_window(lv_timer_t *timer)
{
	ARG_UNUSED(timer);
	if (emulator_window_take_redraw()) {
		/* SDL expose can invalidate its back buffer after the first present. */
		lv_obj_invalidate(lv_screen_active());
	}
}

static int emulator_init(void)
{
	if (lv_display_get_default() == NULL) {
		return -ENODEV;
	}
	int ret = app_ui_reserve_top(EMULATOR_TOOLBAR_HEIGHT);

	if (ret < 0) {
		return ret;
	}
	lv_obj_t *toolbar = lv_obj_create(lv_screen_active());
	lv_obj_set_size(toolbar, lv_display_get_horizontal_resolution(lv_display_get_default()),
			EMULATOR_TOOLBAR_HEIGHT);
	lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);
	lv_obj_set_style_pad_all(toolbar, 0, 0);
	lv_obj_set_style_border_width(toolbar, 0, 0);
	lv_obj_set_style_radius(toolbar, 0, 0);
	lv_obj_set_style_bg_color(toolbar, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(toolbar, LV_OPA_COVER, 0);
	lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(toolbar, 16, 0);

	(void)create_button(toolbar, "Left", DESK_HUB_BUTTON_LEFT);
	(void)create_button(toolbar, "Right", DESK_HUB_BUTTON_RIGHT);
	emulator_window_watch();
	(void)lv_timer_create(refresh_window, 20, NULL);
	return 0;
}

SYS_INIT(emulator_init, APPLICATION, EMULATOR_INIT_PRIORITY);
