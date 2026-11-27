/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_CONWAY_VIEW_H_
#define DESK_HUB_CONWAY_VIEW_H_

#include <stdbool.h>

/** @brief Read one valid grid coordinate from the controller-provided model. */
typedef bool (*conway_view_cell_fn)(int x, int y);

/**
 * @brief Create the view in the shared app container using 4-pixel cells.
 * @param cols Receives the number of visible columns.
 * @param rows Receives the number of visible rows.
 * @return 0 on success, -ENOTSUP when the display cannot fit the grid.
 * @note Main UI thread only. Call deinit before creating the view again.
 */
int conway_view_init(int *cols, int *rows);
/** @brief Update the canvas from the current model state. */
void conway_view_render(conway_view_cell_fn cell, const char *seed_name);
/** @brief Remove the Life view's LVGL objects. */
void conway_view_deinit(void);

#endif /* DESK_HUB_CONWAY_VIEW_H_ */
