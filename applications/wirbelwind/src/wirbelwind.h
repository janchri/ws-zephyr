/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WIRBELWIND_H_
#define WIRBELWIND_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

/* ------------------------------------------------------------------------ */
/* Events (main.c)                                                           */
/* ------------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------------ */
/* SD card (main.c)                                                          */
/* ------------------------------------------------------------------------ */

#define WW_SD_ROOT CONFIG_WW_SD_MOUNT_POINT

/* Same layout as on the ESP32/Arduino version of the box */
#define WW_MUSIC_DIR    "/Music"
#define WW_PLAYLIST_DIR WW_MUSIC_DIR "/.config"

/* Maximum length of an absolute path including the mount point */
#define WW_PATH_MAX 256

/* Build "<mount point><path>" from a card-relative path like "/Music/a.mp3" */
int ww_storage_path(char *buf, size_t len, const char *card_path);

/* mkdir -p for card-relative paths */
int ww_storage_mkdirs(const char *card_path);

/* ------------------------------------------------------------------------ */
/* Playlists (playlist.c)                                                    */
/* ------------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------------ */
/* Player (player.c)                                                         */
/* ------------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------------ */
/* Audio output (audio_*.c), interleaved signed 16 bit PCM                   */
/* ------------------------------------------------------------------------ */

int audio_sink_init(void);
/* (Re)configure and start the output, called before the first write after a stop */
int audio_sink_start(uint32_t rate, uint8_t channels);
/* Blocks until the data is accepted, i.e. paces the caller to real time */
int audio_sink_write(const int16_t *samples, size_t frames);
/* Stop the output (pause or end of track) */
void audio_sink_stop(void);

/* ------------------------------------------------------------------------ */
/* Controller (controller.c): box logic, keys, persistent state              */
/* ------------------------------------------------------------------------ */

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

/* Resume position per playlist, stored in internal flash (settings) */
struct ww_resume {
	uint16_t track;
	uint32_t pos_ms;
};

void ww_resume_load(const char *uuid, struct ww_resume *r);
int ww_resume_save(const char *uuid, const struct ww_resume *r);
int ww_resume_clear(const char *uuid);

/* ------------------------------------------------------------------------ */
/* Web interface (http.c)                                                    */
/* ------------------------------------------------------------------------ */

int ww_http_init(void);

#endif /* WIRBELWIND_H_ */
