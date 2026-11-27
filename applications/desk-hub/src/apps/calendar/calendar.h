/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_CALENDAR_H_
#define DESK_HUB_CALENDAR_H_

/** @brief Run on the main UI thread; return 0 for menu, negative errno on clock failure. */
int desk_hub_calendar_run(void);

#endif /* DESK_HUB_CALENDAR_H_ */
