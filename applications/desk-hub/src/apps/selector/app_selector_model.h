/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_APP_SELECTOR_MODEL_H_
#define DESK_HUB_APP_SELECTOR_MODEL_H_
#include <stddef.h>
/**
 * @brief Move selection one position with wraparound.
 * @param selected Current index, less than count when count is nonzero.
 * @param direction Negative for previous, positive for next, zero to stay.
 * @param count Number of apps; zero returns zero.
 * @return New selection index.
 */
size_t desk_hub_app_selector_move(size_t selected, int direction, size_t count);
#endif
