/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_CALENDAR_VIEW_H_
#define DESK_HUB_CALENDAR_VIEW_H_

#include "calendar_model.h"

/** @brief Create the calendar widgets on the UI thread. Return 0 or negative errno. */
int desk_hub_calendar_view_init(void);
/** @brief Delete the view; safe after partial initialization. UI thread only. */
void desk_hub_calendar_view_deinit(void);
/** @brief Update persistent widgets from valid model data on the UI thread. Return 0. */
int desk_hub_calendar_view_render(const struct desk_hub_calendar_data *data);

#endif /* DESK_HUB_CALENDAR_VIEW_H_ */
