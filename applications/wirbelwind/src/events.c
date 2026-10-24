/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The controller's event queue, fed by the tag reader, the inputs, the player
 * and the user interfaces.
 */

#include <zephyr/logging/log.h>

#include "events.h"

LOG_MODULE_DECLARE(ww, CONFIG_WW_LOG_LEVEL);

K_MSGQ_DEFINE(ww_event_q, sizeof(struct ww_event), 16, 4);

int ww_event_post(const struct ww_event *ev)
{
	int ret = k_msgq_put(&ww_event_q, ev, K_NO_WAIT);

	if (ret) {
		LOG_WRN("Event queue full, dropping event %d", ev->type);
	}

	return ret;
}

int ww_event_get(struct ww_event *ev, k_timeout_t timeout)
{
	return k_msgq_get(&ww_event_q, ev, timeout);
}
