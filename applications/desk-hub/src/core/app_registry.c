/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_registry.h"

#include <zephyr/sys/iterable_sections.h>

size_t desk_hub_app_count(void)
{
	size_t count = 0;

	STRUCT_SECTION_FOREACH(desk_hub_app, app) {
		count++;
	}
	return count;
}

const struct desk_hub_app *desk_hub_app_at(size_t index)
{
	STRUCT_SECTION_FOREACH(desk_hub_app, app) {
		if (index == 0) {
			return app;
		}
		index--;
	}
	return NULL;
}
