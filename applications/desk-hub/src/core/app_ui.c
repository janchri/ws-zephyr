/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_ui.h"
#include <lvgl.h>
#include <errno.h>

static lv_obj_t *content;
static int width;
static int height;
static int reserved_top;

int app_ui_reserve_top(int pixels)
{
	if (content != NULL) {
		return -EALREADY;
	}
	if (pixels < 0) {
		return -EINVAL;
	}
	reserved_top = pixels;
	return 0;
}

int app_ui_init(void)
{
	lv_display_t *lv_display = lv_display_get_default();

	if (lv_display == NULL) {
		return -ENODEV;
	}
	width = lv_display_get_horizontal_resolution(lv_display);
	height = lv_display_get_vertical_resolution(lv_display) - reserved_top;
	if (reserved_top < 0 || height <= 0) {
		return -EINVAL;
	}

	content = lv_obj_create(lv_screen_active());
	lv_obj_set_pos(content, 0, reserved_top);
	lv_obj_set_size(content, width, height);
	lv_obj_set_style_pad_all(content, 0, 0);
	lv_obj_set_style_border_width(content, 0, 0);
	lv_obj_set_style_radius(content, 0, 0);
	lv_obj_set_style_bg_color(content, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
	lv_obj_set_scrollable(content, false);
	return 0;
}

int app_ui_width(void)
{
	return width;
}

int app_ui_height(void)
{
	return height;
}

lv_obj_t *app_ui_root(void)
{
	return content;
}

void app_ui_clear(bool white)
{
	lv_obj_clean(content);
	lv_obj_set_style_bg_color(content, white ? lv_color_white() : lv_color_black(), 0);
}

static lv_obj_t *create_rect(int x, int y, int rect_width, int rect_height)
{
	lv_obj_t *rect = lv_obj_create(content);
	lv_obj_set_pos(rect, x, y);
	lv_obj_set_size(rect, rect_width, rect_height);
	lv_obj_set_style_pad_all(rect, 0, 0);
	lv_obj_set_style_radius(rect, 0, 0);
	lv_obj_set_scrollable(rect, false);
	return rect;
}

void app_ui_fill_rect(int x, int y, int rect_width, int rect_height, bool white)
{
	lv_obj_t *rect = create_rect(x, y, rect_width, rect_height);
	lv_obj_set_style_border_width(rect, 0, 0);
	lv_obj_set_style_bg_color(rect, white ? lv_color_white() : lv_color_black(), 0);
	lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
}

void app_ui_rect(int x, int y, int rect_width, int rect_height, bool white)
{
	lv_obj_t *rect = create_rect(x, y, rect_width, rect_height);
	lv_obj_set_style_bg_opa(rect, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_color(rect, white ? lv_color_white() : lv_color_black(), 0);
	lv_obj_set_style_border_width(rect, 1, 0);
}

static lv_obj_t *create_label(int x, int y, const char *text, int scale, bool white)
{
	lv_obj_t *label = lv_label_create(content);
	lv_label_set_text(label, text);
	lv_obj_set_pos(label, x, y);
	lv_obj_set_style_text_color(label, white ? lv_color_white() : lv_color_black(), 0);
	const lv_font_t *font = scale >= 3   ? &lv_font_montserrat_24
				: scale == 2 ? &lv_font_montserrat_18
					     : &lv_font_montserrat_14;
	lv_obj_set_style_text_font(label, font, 0);
	return label;
}

void app_ui_text(int x, int y, const char *text, int scale, bool white)
{
	(void)create_label(x, y, text, scale, white);
}

void app_ui_text_center(int y, const char *text, int scale, bool white)
{
	app_ui_text_center_at(width / 2, y, text, scale, white);
}

void app_ui_text_center_at(int center_x, int y, const char *text, int scale, bool white)
{
	lv_obj_t *label = create_label(0, y, text, scale, white);
	lv_obj_update_layout(label);
	lv_obj_set_x(label, center_x - lv_obj_get_width(label) / 2);
}

void app_ui_process(void)
{
	(void)lv_timer_handler();
}
