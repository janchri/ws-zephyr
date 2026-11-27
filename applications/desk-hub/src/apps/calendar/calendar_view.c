/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "calendar_view.h"
#include "app_ui.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define MARGIN        16
#define GRID_TOP      144
#define FOOTER_HEIGHT 28

static const char *const months[] = {
	"January", "February", "March",     "April",   "May",      "June",
	"July",    "August",   "September", "October", "November", "December",
};
static const char *const weekdays[] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
static const char *const today_names[] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};

static struct {
	lv_obj_t *root;
	lv_obj_t *clock;
	lv_obj_t *seconds;
	lv_obj_t *date;
	lv_obj_t *weekday;
	lv_obj_t *month;
	lv_obj_t *year;
	lv_obj_t *progress;
	lv_obj_t *highlight;
	lv_obj_t *days[31];
	bool allocation_failed;
	int width;
	int height;
	int column_width;
	int row_height;
	int shown_month;
	int shown_year;
	int shown_today;
} view;

static lv_obj_t *label(const lv_font_t *font, bool white)
{
	lv_obj_t *obj = lv_label_create(view.root);
	if (obj == NULL) {
		view.allocation_failed = true;
		return NULL;
	}
	lv_obj_set_style_text_font(obj, font, 0);
	lv_obj_set_style_text_color(obj, white ? lv_color_white() : lv_color_black(), 0);
	return obj;
}

static void text_at(lv_obj_t *obj, const char *text, int x, int y, int width, lv_text_align_t align)
{
	if (obj == NULL) {
		return;
	}
	const char *previous = lv_label_get_text(obj);
	if (previous == NULL || strcmp(previous, text) != 0) {
		lv_label_set_text(obj, text);
	}
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_width(obj, width);
	lv_obj_set_style_text_align(obj, align, 0);
}

static lv_obj_t *box(int x, int y, int width, int height, int radius)
{
	lv_obj_t *obj = lv_obj_create(view.root);
	if (obj == NULL) {
		view.allocation_failed = true;
		return NULL;
	}
	lv_obj_remove_style_all(obj);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_size(obj, width, height);
	lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(obj, radius, 0);
	lv_obj_set_scrollable(obj, false);
	return obj;
}

int desk_hub_calendar_view_init(void)
{
	view.width = app_ui_width();
	view.height = app_ui_height();
	if (view.width < 360 || view.height < 300) {
		return -ENOTSUP;
	}
	view.column_width = (view.width - 2 * MARGIN) / 7;
	view.row_height = (view.height - FOOTER_HEIGHT - GRID_TOP) / 6;
	view.shown_month = -1;
	view.root = lv_obj_create(app_ui_root());
	if (view.root == NULL) {
		return -ENOMEM;
	}
	lv_obj_remove_style_all(view.root);
	lv_obj_set_size(view.root, view.width, view.height);
	lv_obj_set_style_bg_color(view.root, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(view.root, LV_OPA_COVER, 0);
	lv_obj_set_scrollable(view.root, false);

	text_at(label(&lv_font_montserrat_14, false), "TODAY  /  UTC", MARGIN, 8, 220,
		LV_TEXT_ALIGN_LEFT);
	view.clock = label(&lv_font_montserrat_40, false);
	view.seconds = label(&lv_font_montserrat_18, false);
	box(MARGIN, 77, 184, 1, 0);
	view.progress = box(MARGIN, 75, 3, 4, 0);
	box(view.width - MARGIN - 68, 8, 68, 70, 8);
	view.date = label(&lv_font_montserrat_40, true);
	view.weekday = label(&lv_font_montserrat_18, true);
	view.month = label(&lv_font_montserrat_24, false);
	view.year = label(&lv_font_montserrat_14, false);

	for (int col = 0; col < 7; col++) {
		text_at(label(&lv_font_montserrat_14, false), weekdays[col],
			MARGIN + col * view.column_width, 122, view.column_width,
			LV_TEXT_ALIGN_CENTER);
	}
	view.highlight = box(0, 0, 32, view.row_height - 1, 5);
	for (int day = 0; day < 31; day++) {
		view.days[day] = label(&lv_font_montserrat_18, false);
	}
	box(MARGIN, view.height - FOOTER_HEIGHT + 1, view.width - 2 * MARGIN, 1, 0);
	text_at(label(&lv_font_montserrat_14, false), "<  Month  >", MARGIN, view.height - 20, 140,
		LV_TEXT_ALIGN_LEFT);
	text_at(label(&lv_font_montserrat_14, false), "Hold Left: menu", view.width - 190,
		view.height - 20, 174, LV_TEXT_ALIGN_RIGHT);
	return view.allocation_failed ? -ENOMEM : 0;
}

int desk_hub_calendar_view_render(const struct desk_hub_calendar_data *data)
{
	char text[32];

	snprintf(text, sizeof(text), "%02d:%02d", data->hour, data->minute);
	text_at(view.clock, text, MARGIN - 2, 28, 160, LV_TEXT_ALIGN_LEFT);
	snprintf(text, sizeof(text), "/ %02d", data->second);
	text_at(view.seconds, text, 174, 46, 70, LV_TEXT_ALIGN_LEFT);
	lv_obj_set_x(view.progress, MARGIN + data->second * 181 / 59);
	snprintf(text, sizeof(text), "%02d", data->current_day);
	text_at(view.date, text, view.width - MARGIN - 68, 10, 68, LV_TEXT_ALIGN_CENTER);
	text_at(view.weekday, today_names[data->current_weekday], view.width - MARGIN - 68, 56, 68,
		LV_TEXT_ALIGN_CENTER);

	if (view.shown_month == data->month && view.shown_year == data->year &&
	    view.shown_today == data->today) {
		return 0;
	}
	view.shown_month = data->month;
	view.shown_year = data->year;
	view.shown_today = data->today;
	text_at(view.month, months[data->month], MARGIN, 90, 230, LV_TEXT_ALIGN_LEFT);
	snprintf(text, sizeof(text), "%04d  /  %02d", data->year, data->month + 1);
	text_at(view.year, text, view.width - 146, 98, 130, LV_TEXT_ALIGN_RIGHT);
	lv_obj_set_hidden(view.highlight, data->today < 1);
	for (int day = 1; day <= 31; day++) {
		lv_obj_t *obj = view.days[day - 1];
		lv_obj_set_hidden(obj, day > data->days_in_month);
		if (day > data->days_in_month) {
			continue;
		}
		const int position = data->first_weekday + day - 1;
		const int x = MARGIN + position % 7 * view.column_width;
		const int y = GRID_TOP + position / 7 * view.row_height;
		const bool today = day == data->today;

		snprintf(text, sizeof(text), "%d", day);
		text_at(obj, text, x, y, view.column_width, LV_TEXT_ALIGN_CENTER);
		lv_obj_set_style_text_color(obj, today ? lv_color_white() : lv_color_black(), 0);
		if (today) {
			lv_obj_set_pos(view.highlight, x + (view.column_width - 32) / 2, y);
		}
	}
	return 0;
}

void desk_hub_calendar_view_deinit(void)
{
	if (view.root != NULL) {
		lv_obj_delete(view.root);
	}
	memset(&view, 0, sizeof(view));
}
