/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_EMULATOR_WINDOW_H_
#define DESK_HUB_EMULATOR_WINDOW_H_
#include <stdbool.h>
/** @brief Watch host SDL expose events. Call once during emulator initialization. */
void emulator_window_watch(void);
/** @brief Atomically consume the pending redraw request on the UI thread. */
bool emulator_window_take_redraw(void);
#endif
