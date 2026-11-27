/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_APP_UI_H_
#define DESK_HUB_APP_UI_H_

#include <stdbool.h>
#include <lvgl.h>

/**
 * @file
 * @brief Shared view primitives. All coordinates and dimensions are in pixels,
 * relative to the app container; all functions run on the main UI thread.
 * Text is copied by LVGL. Scale 1 selects 14px, 2 selects 18px, 3 selects 24px.
 * Widgets are owned by the container and released by app_ui_clear().
 */

/**
 * @brief Reserve top pixels for platform controls before creating app content.
 * @param pixels Nonnegative inset from the display's top edge; defaults to zero.
 * @return 0 on success, -EINVAL for negative pixels, -EALREADY after app_ui_init().
 * @note Called during platform initialization, before any UI loop is running.
 */
int app_ui_reserve_top(int pixels);
/** @brief Create app content using platform-reserved space. Return 0 or negative errno. */
int app_ui_init(void);
/** @brief Return the logical display width in pixels. */
int app_ui_width(void);
/** @brief Return the logical display height in pixels. */
int app_ui_height(void);
/** @brief Borrow the app container; parent all app widgets here. Do not delete it. */
lv_obj_t *app_ui_root(void);
/** @brief Remove app content objects and set its background. */
void app_ui_clear(bool white);
/** @brief Add a filled rectangle to the active app screen. */
void app_ui_fill_rect(int x, int y, int width, int height, bool white);
/** @brief Add a one-pixel outlined rectangle to the active app screen. */
void app_ui_rect(int x, int y, int width, int height, bool white);
/** @brief Add a label at the requested position. */
void app_ui_text(int x, int y, const char *text, int scale, bool white);
/** @brief Add a horizontally centered label. */
void app_ui_text_center(int y, const char *text, int scale, bool white);
/** @brief Add a label centered on a horizontal pixel coordinate. */
void app_ui_text_center_at(int center_x, int y, const char *text, int scale, bool white);
/** @brief Service pending LVGL rendering and input on the main thread. */
void app_ui_process(void);

#endif /* DESK_HUB_APP_UI_H_ */
