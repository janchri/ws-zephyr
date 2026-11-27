/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_APP_SELECTOR_VIEW_H_
#define DESK_HUB_APP_SELECTOR_VIEW_H_

#include <stddef.h>

/** Maximum visible tiles; the controller handles pagination. */
#define APP_SELECTOR_PAGE_SIZE 4

/**
 * @brief Replace the shared app content with a page of named tiles.
 * @param names Borrowed display names; copied into labels during this call.
 * @param count Number of visible tiles (1..APP_SELECTOR_PAGE_SIZE).
 * @param selected Highlighted index within this page, less than count.
 * @return 0 on success, -EINVAL for an invalid page.
 * @note Main UI thread only; rendering is serviced by the shared runtime.
 */
int desk_hub_app_selector_view_render(const char *const names[], size_t count, size_t selected);

#endif /* DESK_HUB_APP_SELECTOR_VIEW_H_ */
