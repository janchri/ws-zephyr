/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_CLOCK_H_
#define DESK_HUB_CLOCK_H_
/** @brief Preserve a valid system clock, otherwise seed it with build time (UTC). */
void desk_hub_clock_init(void);
#endif
