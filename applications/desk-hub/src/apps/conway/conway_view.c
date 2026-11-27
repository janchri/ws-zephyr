/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "conway_view.h"
#include "app_ui.h"

#include <errno.h>
#include <string.h>
#include <lvgl.h>
#include <zephyr/sys/util.h>

#define CELL_SIZE       4
#define HEADER_HEIGHT   24
#define FOOTER_HEIGHT   46
#define VIEW_MAX_WIDTH  400
#define VIEW_MAX_HEIGHT 400

static uint8_t canvas_buffer[LV_CANVAS_BUF_SIZE(VIEW_MAX_WIDTH, VIEW_MAX_HEIGHT, 1, 1)]
	__aligned(4);
static lv_obj_t *canvas;
static lv_obj_t *seed_label;
static lv_obj_t *navigation_label;
static int cols;
static int rows;
static int cell_size;
static bool previous[VIEW_MAX_HEIGHT / CELL_SIZE][VIEW_MAX_WIDTH / CELL_SIZE];
static char previous_seed[32];
static bool first_render = true;

int conway_view_init(int *width_cells, int *height_cells)
{
	lv_obj_t *screen = app_ui_root();
	const int size = CELL_SIZE;
	const int width = app_ui_width() / size * size;
	const int height = (app_ui_height() - HEADER_HEIGHT - FOOTER_HEIGHT) / size * size;

	if (width <= 0 || height <= 0 || width > VIEW_MAX_WIDTH || height > VIEW_MAX_HEIGHT) {
		return -ENOTSUP;
	}
	cols = *width_cells = width / size;
	rows = *height_cells = height / size;
	cell_size = size;
	canvas = lv_canvas_create(screen);
	lv_canvas_set_buffer(canvas, canvas_buffer, width, height, LV_COLOR_FORMAT_I1);
	lv_draw_buf_set_palette(lv_canvas_get_draw_buf(canvas), 0,
				lv_color32_make(0, 0, 0, LV_OPA_COVER));
	lv_draw_buf_set_palette(lv_canvas_get_draw_buf(canvas), 1,
				lv_color32_make(255, 255, 255, LV_OPA_COVER));
	lv_obj_set_pos(canvas, 0, HEADER_HEIGHT);
	lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

	seed_label = lv_label_create(screen);
	lv_obj_set_pos(seed_label, 8, 2);
	lv_obj_set_style_text_color(seed_label, lv_color_white(), 0);
	lv_obj_set_style_text_font(seed_label, &lv_font_montserrat_18, 0);

	navigation_label = lv_label_create(screen);
	lv_label_set_text(navigation_label, "LEFT / RIGHT: SEED\nHOLD LEFT: MENU");
	lv_obj_set_width(navigation_label, app_ui_width());
	lv_obj_set_height(navigation_label, 44);
	lv_obj_set_pos(navigation_label, 0, app_ui_height() - FOOTER_HEIGHT);
	lv_obj_set_style_text_align(navigation_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_text_color(navigation_label, lv_color_white(), 0);
	lv_obj_set_style_text_font(navigation_label, &lv_font_montserrat_18, 0);
	return 0;
}

void conway_view_render(conway_view_cell_fn cell, const char *seed_name)
{
	const bool reset = first_render || strcmp(previous_seed, seed_name) != 0;

	lv_label_set_text(seed_label, seed_name);
	if (reset) {
		lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
		strncpy(previous_seed, seed_name, sizeof(previous_seed) - 1);
		previous_seed[sizeof(previous_seed) - 1] = '\0';
	}
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			const bool alive = cell(x, y);

			if (!reset && alive == previous[y][x]) {
				continue;
			}
			for (int dy = 0; dy < cell_size; dy++) {
				for (int dx = 0; dx < cell_size; dx++) {
					lv_canvas_set_px(
						canvas, x * cell_size + dx, y * cell_size + dy,
						alive ? lv_color_black() : lv_color_white(),
						LV_OPA_COVER);
				}
			}
			previous[y][x] = alive;
		}
	}
	first_render = false;
}

void conway_view_deinit(void)
{
	if (canvas != NULL) {
		lv_obj_delete(canvas);
		canvas = NULL;
	}
	if (seed_label != NULL) {
		lv_obj_delete(seed_label);
		seed_label = NULL;
	}
	if (navigation_label != NULL) {
		lv_obj_delete(navigation_label);
		navigation_label = NULL;
	}
	first_render = true;
}
