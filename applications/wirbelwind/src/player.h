/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* MP3 player thread: SD card -> decoder -> volume -> audio output */

#ifndef WW_PLAYER_H_
#define WW_PLAYER_H_

#include <stdint.h>

enum ww_player_state {
	WW_PLAYER_STOPPED,
	WW_PLAYER_PLAYING,
	WW_PLAYER_PAUSED,
};

struct ww_player_status {
	enum ww_player_state state;
	uint32_t pos_ms;
	uint32_t rate;
	uint8_t channels;
	uint16_t bitrate_kbps;
};

/* Called from the player thread when a track played to its end (err == 0) or failed */
typedef void (*ww_player_done_cb_t)(int err);

int ww_player_init(ww_player_done_cb_t done_cb);

/* All calls below are synchronous: they return once the player thread handled them */
int ww_player_play(const char *card_path, uint32_t start_ms);
void ww_player_pause(void);
void ww_player_resume(void);
void ww_player_stop(void);
int ww_player_seek(uint32_t pos_ms);

void ww_player_set_volume(uint8_t volume);
void ww_player_get_status(struct ww_player_status *st);

const char *ww_player_state_str(enum ww_player_state state);

#endif /* WW_PLAYER_H_ */
