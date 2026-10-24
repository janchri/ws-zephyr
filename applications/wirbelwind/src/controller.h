/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* The box logic: tags select playlists, the button and the UIs control playback */

#ifndef WW_CONTROLLER_H_
#define WW_CONTROLLER_H_

#include <stdbool.h>
#include <stddef.h>

#include "events.h"
#include "player.h"

struct ww_status {
	bool tag_present;
	char uid[WW_UID_STR_LEN];
	char name[64];
	size_t track;
	size_t num_tracks;
	char track_path[128];
	uint8_t volume;
	struct ww_player_status player;
};

int ww_controller_init(void);
void ww_controller_get_status(struct ww_status *st);

/*
 * Play track (from its start) of a playlist: right away if its tag is on the
 * box, otherwise as soon as the tag is placed the next time.
 */
int ww_controller_cue(const char *uuid, size_t track);

#endif /* WW_CONTROLLER_H_ */
