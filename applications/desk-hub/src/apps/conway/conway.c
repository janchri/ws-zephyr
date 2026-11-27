/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "conway.h"

#include "conway_model.h"
#include "conway_view.h"
#include "event.h"
#include "app_registry.h"
#include "app_runtime.h"

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#define TICK_MS 100

static enum conway_seed seed;

static void random_bytes(uint8_t *buffer, size_t length)
{
	sys_rand_get(buffer, length);
}

static int conway_on_entry(void)
{
	int cols;
	int rows;
	seed = CONWAY_SEED_ACHIMS_P144;
	int ret;

	ret = conway_view_init(&cols, &rows);
	if (ret < 0) {
		return ret;
	}
	ret = conway_model_init(cols, rows, seed, random_bytes);
	if (ret < 0) {
		return ret;
	}
	conway_view_render(conway_model_cell, conway_model_seed_name(seed));

	return 0;
}

static void conway_on_exit(void)
{
	conway_view_deinit();
}

int desk_hub_conway_run(void)
{
	int64_t next_tick = k_uptime_get() + TICK_MS;

	while (true) {
		enum desk_hub_event event;

		if (app_runtime_next_event(&event) == 0) {
			if (event == DESK_HUB_EVENT_LONG_LEFT) {
				return 0;
			}
			if (event == DESK_HUB_EVENT_SHORT_LEFT ||
			    event == DESK_HUB_EVENT_SHORT_RIGHT) {
				const int direction = event == DESK_HUB_EVENT_SHORT_RIGHT ? 1 : -1;

				seed = (seed + CONWAY_SEED_COUNT + direction) % CONWAY_SEED_COUNT;
				conway_model_set_seed(seed);
				next_tick = k_uptime_get() + TICK_MS;
				conway_view_render(conway_model_cell, conway_model_seed_name(seed));
			}
		}

		if (k_uptime_get() >= next_tick) {
			conway_model_step();
			conway_view_render(conway_model_cell, conway_model_seed_name(seed));
			next_tick = k_uptime_get() + TICK_MS;
		}
	}
}

DESK_HUB_APP_REGISTER(app_a_conway, "GAME OF LIFE", desk_hub_conway_run,
		      .on_entry = conway_on_entry, .on_exit = conway_on_exit);
