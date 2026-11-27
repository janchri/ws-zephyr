/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_EVENT_H_
#define DESK_HUB_EVENT_H_

#include <stdbool.h>

#include <zephyr/kernel.h>

enum desk_hub_button {
	DESK_HUB_BUTTON_LEFT,
	DESK_HUB_BUTTON_RIGHT,
};

enum desk_hub_event {
	DESK_HUB_EVENT_SHORT_LEFT,
	DESK_HUB_EVENT_SHORT_RIGHT,
	DESK_HUB_EVENT_LONG_LEFT,
	DESK_HUB_EVENT_LONG_RIGHT,
};

/**
 * @brief Report a physical or emulator button press/release.
 *
 * @param button Button that changed state.
 * @param pressed True while held, false when released.
 */
void desk_hub_button_state(enum desk_hub_button button, bool pressed);

/**
 * @brief Wait for the next normalized button event.
 *
 * @param event Receives the event.
 * @param timeout Maximum wait duration.
 * @return 0 on success, or a negative errno value on timeout/error.
 */
int desk_hub_event_get(enum desk_hub_event *event, k_timeout_t timeout);

#endif /* DESK_HUB_EVENT_H_ */
