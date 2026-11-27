/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Compiled in the native runner, with the host SDL and C library. */
#include "emulator_window.h"
#include <SDL.h>
#include <stdatomic.h>

static atomic_bool redraw_pending;

static int window_event(void *context, SDL_Event *event)
{
	(void)context;
	if (event->type == SDL_WINDOWEVENT && (event->window.event == SDL_WINDOWEVENT_EXPOSED ||
					       event->window.event == SDL_WINDOWEVENT_SHOWN ||
					       event->window.event == SDL_WINDOWEVENT_RESTORED)) {
		atomic_store(&redraw_pending, true);
	}
	return 1;
}

void emulator_window_watch(void)
{
	atomic_store(&redraw_pending, true);
	SDL_AddEventWatch(window_event, NULL);
}

bool emulator_window_take_redraw(void)
{
	return atomic_exchange(&redraw_pending, false);
}
