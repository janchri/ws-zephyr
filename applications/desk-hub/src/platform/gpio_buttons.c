/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "event.h"
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>

#if defined(CONFIG_INPUT_GPIO_KEYS) && DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(sw0)) &&                   \
	DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(sw1))
#define BOOT_BUTTON DT_ALIAS(sw0)
#define KEY_BUTTON  DT_ALIAS(sw1)

static void gpio_button_event(struct input_event *event, void *user_data)
{
	enum desk_hub_button button;

	ARG_UNUSED(user_data);

	if (event->type != INPUT_EV_KEY || (event->value != 0 && event->value != 1)) {
		return;
	}

	switch (event->code) {
	case DT_PROP(BOOT_BUTTON, zephyr_code):
		button = DESK_HUB_BUTTON_LEFT;
		break;
	case DT_PROP(KEY_BUTTON, zephyr_code):
		button = DESK_HUB_BUTTON_RIGHT;
		break;
	default:
		return;
	}

	desk_hub_button_state(button, event->value == 1);
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_PARENT(BOOT_BUTTON)), gpio_button_event, NULL);
#endif
