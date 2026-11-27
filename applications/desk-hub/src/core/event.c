/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "event.h"

#define LONG_PRESS_MS 600

struct button_state {
	bool pressed;
	bool long_sent;
};

static struct button_state buttons[2];
static struct k_spinlock button_lock;

K_MSGQ_DEFINE(button_events, sizeof(enum desk_hub_event), 8, 4);

static void publish_event(enum desk_hub_event event)
{
	(void)k_msgq_put(&button_events, &event, K_NO_WAIT);
}

static void left_long_press(struct k_work *work)
{
	k_spinlock_key_t key = k_spin_lock(&button_lock);
	bool publish =
		buttons[DESK_HUB_BUTTON_LEFT].pressed && !buttons[DESK_HUB_BUTTON_LEFT].long_sent;

	if (publish) {
		buttons[DESK_HUB_BUTTON_LEFT].long_sent = true;
	}
	k_spin_unlock(&button_lock, key);

	if (publish) {
		publish_event(DESK_HUB_EVENT_LONG_LEFT);
	}
}

static void right_long_press(struct k_work *work)
{
	k_spinlock_key_t key = k_spin_lock(&button_lock);
	bool publish =
		buttons[DESK_HUB_BUTTON_RIGHT].pressed && !buttons[DESK_HUB_BUTTON_RIGHT].long_sent;

	if (publish) {
		buttons[DESK_HUB_BUTTON_RIGHT].long_sent = true;
	}
	k_spin_unlock(&button_lock, key);

	if (publish) {
		publish_event(DESK_HUB_EVENT_LONG_RIGHT);
	}
}

K_WORK_DELAYABLE_DEFINE(left_hold_work, left_long_press);
K_WORK_DELAYABLE_DEFINE(right_hold_work, right_long_press);

void desk_hub_button_state(enum desk_hub_button button, bool pressed)
{
	struct button_state *state;
	struct k_work_delayable *hold_work;
	enum desk_hub_event short_event;
	bool publish_short = false;
	k_spinlock_key_t key;

	if (button != DESK_HUB_BUTTON_LEFT && button != DESK_HUB_BUTTON_RIGHT) {
		return;
	}

	state = &buttons[button];
	hold_work = button == DESK_HUB_BUTTON_LEFT ? &left_hold_work : &right_hold_work;
	short_event = button == DESK_HUB_BUTTON_LEFT ? DESK_HUB_EVENT_SHORT_LEFT
						     : DESK_HUB_EVENT_SHORT_RIGHT;

	key = k_spin_lock(&button_lock);
	if (pressed) {
		if (state->pressed) {
			k_spin_unlock(&button_lock, key);
			return;
		}
		state->pressed = true;
		state->long_sent = false;
		k_spin_unlock(&button_lock, key);
		(void)k_work_reschedule(hold_work, K_MSEC(LONG_PRESS_MS));
		return;
	}

	if (!state->pressed) {
		k_spin_unlock(&button_lock, key);
		return;
	}
	publish_short = !state->long_sent;
	state->pressed = false;
	state->long_sent = false;
	k_spin_unlock(&button_lock, key);
	(void)k_work_cancel_delayable(hold_work);

	if (publish_short) {
		publish_event(short_event);
	}
}

int desk_hub_event_get(enum desk_hub_event *event, k_timeout_t timeout)
{
	return k_msgq_get(&button_events, event, timeout);
}
