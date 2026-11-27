/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_APP_RUNTIME_H_
#define DESK_HUB_APP_RUNTIME_H_
#include "event.h"
#include "app_registry.h"

/**
 * @brief Run entry, event loop and exit hooks on the main UI thread.
 * @param app Valid descriptor with an on_run callback.
 * @return Entry/run status, or -EINVAL for a missing descriptor/on_run.
 * Entry failure skips on_run. on_exit runs after any attempted entry, including
 * failure. The shared UI is cleared before entry and after exit. Do not nest
 * this function: one app owns the UI at a time.
 */
int app_runtime_run(const struct desk_hub_app *app);
/**
 * @brief Service the UI and wait at most 10 ms for a normalized input event.
 * @param event Receives the next event; unmodified on timeout.
 * @return 0 on success; negative errno if no event is available.
 * @note Call from the main thread only, once per controller loop.
 */
int app_runtime_next_event(enum desk_hub_event *event);
#endif
