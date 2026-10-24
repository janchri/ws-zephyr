/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WW_PLAYLIST_H_
#define WW_PLAYLIST_H_

#include <stdbool.h>
#include <stddef.h>

#include "events.h"

/*
 * A playlist is a JSON file on the SD card, compatible with the Arduino
 * version of the box:
 *
 *   /Music/.config/playlist_<uuid>
 *   {"uuid":"0a1b2c3d","name":"Bibi","tracks":["/Music/Bibi/01.mp3", ...]}
 *
 * The uuid is the UID of the tag the playlist belongs to.
 */

struct ww_playlist_data {
	const char *uuid;
	const char *name;
	const char *tracks[CONFIG_WW_PLAYLIST_MAX_TRACKS];
	size_t num_tracks;
};

struct ww_playlist {
	struct ww_playlist_data d;
	/* Parsed JSON; strings added at runtime are appended behind it */
	char buf[CONFIG_WW_PLAYLIST_BUF_SIZE];
	size_t used;
	char uuid[WW_UID_STR_LEN];
};

bool ww_playlist_uuid_valid(const char *uuid);

int ww_playlist_load(struct ww_playlist *pl, const char *uuid);
/* Create (and save) an empty playlist */
int ww_playlist_create(struct ww_playlist *pl, const char *uuid, const char *name);
int ww_playlist_delete(const char *uuid);

/* The modifiers below save the playlist and reload it */
int ww_playlist_set_name(struct ww_playlist *pl, const char *name);
/* Adds one .mp3 file, or all .mp3 files of a directory (sorted). Returns number added */
int ww_playlist_add(struct ww_playlist *pl, const char *card_path);
int ww_playlist_remove_track(struct ww_playlist *pl, size_t idx);

typedef int (*ww_playlist_cb_t)(const char *uuid, void *user_data);
/* Calls cb for every stored playlist; stops early if cb returns non-zero */
int ww_playlist_foreach(ww_playlist_cb_t cb, void *user_data);

/*
 * Shared scratch playlist for the user interfaces (shell, web). Lock it,
 * work on it, unlock it; unlocking tells the controller to reload.
 */
struct ww_playlist *ww_playlist_scratch_lock(void);
void ww_playlist_scratch_unlock(bool modified);

#endif /* WW_PLAYLIST_H_ */
