/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Events for the controller: tags, button, end of track, commands from the UIs */

#ifndef WW_EVENTS_H_
#define WW_EVENTS_H_

#include <stdint.h>

#include <zephyr/kernel.h>

/* Hex string of a tag UID (up to 10 bytes) */
#define WW_UID_STR_LEN 21

enum ww_event_type {
	WW_EV_TAG_PLACED,
	WW_EV_TAG_REMOVED,
	WW_EV_BUTTON,
	WW_EV_TRACK_DONE,
	WW_EV_CMD,
};

enum ww_press {
	WW_PRESS_SHORT,
	WW_PRESS_LONG,
	WW_PRESS_LONGLONG,
};

/* On the box itself the tag decides about play/pause; these are for the web UI */
enum ww_cmd {
	WW_CMD_PAUSE,
	WW_CMD_RESUME, /* only while the tag is on the box */
	WW_CMD_NEXT,
	WW_CMD_VOLUME, /* arg: absolute volume */
	WW_CMD_SEEK,   /* arg: position in seconds */
	WW_CMD_TRACK,  /* arg: track index */
	WW_CMD_RESET,  /* restart the current playlist from the first track */
	WW_CMD_RESET_ALL, /* forget the positions of all playlists */
	WW_CMD_RELOAD, /* re-read the current playlist from the SD card */
};

struct ww_event {
	enum ww_event_type type;
	union {
		struct {
			char uid[WW_UID_STR_LEN];
		} tag;
		enum ww_press press;
		struct {
			enum ww_cmd cmd;
			int32_t arg;
		} cmd;
	};
};

int ww_event_post(const struct ww_event *ev);
int ww_event_get(struct ww_event *ev, k_timeout_t timeout);

static inline int ww_event_post_cmd(enum ww_cmd cmd, int32_t arg)
{
	struct ww_event ev = {.type = WW_EV_CMD, .cmd = {.cmd = cmd, .arg = arg}};

	return ww_event_post(&ev);
}

#endif /* WW_EVENTS_H_ */
