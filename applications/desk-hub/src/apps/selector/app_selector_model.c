/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_selector_model.h"

size_t desk_hub_app_selector_move(size_t selected, int direction, size_t count)
{
	if (count == 0) {
		return 0;
	}
	if (direction < 0) {
		return selected == 0 ? count - 1 : selected - 1;
	}
	if (direction > 0) {
		return selected + 1 == count ? 0 : selected + 1;
	}
	return selected;
}
