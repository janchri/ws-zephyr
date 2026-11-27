/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_selector_view.h"

#include <errno.h>

#include "app_ui.h"

#include <zephyr/sys/util.h>

int desk_hub_app_selector_view_render(const char *const names[], size_t count, size_t selected)
{
	if (count == 0 || count > APP_SELECTOR_PAGE_SIZE || selected >= count) {
		return -EINVAL;
	}
	const int width = app_ui_width();
	const int height = app_ui_height();
	const int margin = 20;
	const int gap = 12;
	const size_t visible_count = count;
	const int columns = MIN(visible_count, 2);
	const int rows = (visible_count + columns - 1) / columns;
	const int tile_width = (width - 2 * margin - gap * (columns - 1)) / columns;
	const int available_height = height - 64 - (52);
	const int tile_height = MIN(108, (available_height - gap * (rows - 1)) / rows);

	app_ui_clear(false);
	app_ui_text_center(16, "APPS", 2, true);

	for (size_t i = 0; i < visible_count; i++) {
		const size_t app_index = i;
		const int col = i % columns;
		const int row = i / columns;
		const int x = margin + col * (tile_width + gap);
		const int tile_y = 52 + row * (tile_height + gap);
		const bool active = app_index == selected;
		const bool text_white = !active;

		if (active) {
			app_ui_fill_rect(x, tile_y, tile_width, tile_height, true);
		} else {
			app_ui_rect(x, tile_y, tile_width, tile_height, true);
		}
		app_ui_text_center_at(x + tile_width / 2, tile_y + (tile_height - 22) / 2,
				      names[app_index], 2, text_white);
	}

	app_ui_text_center(height - 57, "LEFT / RIGHT: SELECT", 2, true);
	app_ui_text_center(height - 31, "HOLD RIGHT: OPEN", 2, true);
	return 0;
}
