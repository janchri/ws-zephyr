/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "display_backend.h"

#include <errno.h>
#include <lvgl.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>

#define DISPLAY_NODE DT_CHOSEN(zephyr_display)

int desk_hub_display_backend_init(void)
{
	const struct device *display = DEVICE_DT_GET(DISPLAY_NODE);
	int ret;

	if (!device_is_ready(display)) {
		return -ENODEV;
	}
	ret = display_set_pixel_format(display, PIXEL_FORMAT_MONO01);
	if (ret < 0) {
		return ret;
	}
	lv_display_t *lv_display = lv_display_get_default();
	if (lv_display == NULL) {
		return -ENODEV;
	}
	if (DT_NODE_HAS_COMPAT(DISPLAY_NODE, sitronix_st7306)) {
		lv_display_set_rotation(lv_display, LV_DISPLAY_ROTATION_90);
	}
	return display_blanking_off(display);
}
