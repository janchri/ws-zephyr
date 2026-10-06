/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The box logic: tags select playlists, keys control playback, the position
 * of every playlist is remembered (settings in internal flash, independent of
 * the SD card).
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include "wirbelwind.h"

LOG_MODULE_DECLARE(ww, CONFIG_WW_LOG_LEVEL);

#define POT_NODE  DT_NODELABEL(volume_pot)
#define POT_RANGE DT_PROP(DT_CHILD(POT_NODE, axis), out_max)

static K_MUTEX_DEFINE(ctl_lock);
static struct ww_playlist pl;
static bool pl_loaded;
static bool tag_present;
static size_t cur_track;
static uint8_t volume;
static int64_t last_save;

/* Persistent state */

#define KEY_RESUME "ww/pos"

struct read_ctx {
	void *dst;
	size_t len;
	bool found;
};

static int read_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
		   void *param)
{
	struct read_ctx *ctx = param;

	/* Only accept the exact key, not children of it */
	if (key != NULL || len != ctx->len) {
		return 0;
	}
	if (read_cb(cb_arg, ctx->dst, ctx->len) == ctx->len) {
		ctx->found = true;
	}

	return 0;
}

static bool read_value(const char *key, void *dst, size_t len)
{
	struct read_ctx ctx = {.dst = dst, .len = len};

	settings_load_subtree_direct(key, read_cb, &ctx);

	return ctx.found;
}

void ww_resume_load(const char *uuid, struct ww_resume *r)
{
	char key[sizeof(KEY_RESUME) + WW_UID_STR_LEN];

	snprintf(key, sizeof(key), KEY_RESUME "/%s", uuid);
	if (!read_value(key, r, sizeof(*r))) {
		memset(r, 0, sizeof(*r));
	}
}

int ww_resume_save(const char *uuid, const struct ww_resume *r)
{
	char key[sizeof(KEY_RESUME) + WW_UID_STR_LEN];

	snprintf(key, sizeof(key), KEY_RESUME "/%s", uuid);

	return settings_save_one(key, r, sizeof(*r));
}

int ww_resume_clear(const char *uuid)
{
	char key[sizeof(KEY_RESUME) + WW_UID_STR_LEN];

	snprintf(key, sizeof(key), KEY_RESUME "/%s", uuid);

	return settings_delete(key);
}

#define CLEAR_BATCH 16

struct clear_ctx {
	char keys[CLEAR_BATCH][WW_UID_STR_LEN];
	size_t count;
};

static int collect_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
		      void *param)
{
	struct clear_ctx *ctx = param;

	if (key != NULL && ctx->count < CLEAR_BATCH) {
		strncpy(ctx->keys[ctx->count], key, WW_UID_STR_LEN - 1);
		ctx->keys[ctx->count][WW_UID_STR_LEN - 1] = '\0';
		ctx->count++;
	}

	return 0;
}

/* Forget the positions of all playlists */
static void resume_clear_all(void)
{
	struct clear_ctx ctx;

	/* Collect in batches, deleting while iterating is not allowed */
	do {
		ctx.count = 0;
		settings_load_subtree_direct(KEY_RESUME, collect_cb, &ctx);
		for (size_t i = 0; i < ctx.count; i++) {
			ww_resume_clear(ctx.keys[i]);
		}
	} while (ctx.count == CLEAR_BATCH);

	/* Volume stored by older versions */
	settings_delete("ww/volume");
}

/* Box logic */

static enum ww_player_state player_state(void)
{
	struct ww_player_status st;

	ww_player_get_status(&st);

	return st.state;
}

static void save_resume(void)
{
	struct ww_player_status st;
	struct ww_resume r;

	if (!pl_loaded) {
		return;
	}

	ww_player_get_status(&st);
	r.track = cur_track;
	r.pos_ms = (st.state == WW_PLAYER_STOPPED) ? 0 : st.pos_ms;
	ww_resume_save(pl.uuid, &r);
	last_save = k_uptime_get();
	LOG_DBG("Saved %s: track %u @ %u ms", pl.uuid, r.track, r.pos_ms);
}

/* Starts track idx; skips unplayable tracks. */
static int start_track(size_t idx, uint32_t pos_ms)
{
	size_t n = pl.d.num_tracks;

	for (size_t tries = 0; tries < n; tries++, idx = (idx + 1) % n, pos_ms = 0) {
		cur_track = idx;
		LOG_INF("Track %u/%u: %s", idx + 1, n, pl.d.tracks[idx]);
		if (ww_player_play(pl.d.tracks[idx], pos_ms) == 0) {
			return 0;
		}
	}

	LOG_ERR("No playable track in playlist %s", pl.uuid);

	return -ENOENT;
}

static bool can_play(void)
{
	if (!tag_present || !pl_loaded) {
		LOG_INF("No tag");
		return false;
	}
	if (pl.d.num_tracks == 0) {
		LOG_INF("Playlist %s is empty", pl.uuid);
		return false;
	}

	return true;
}

static void set_volume(int v)
{
	volume = CLAMP(v, 0, CONFIG_WW_VOLUME_MAX);
	ww_player_set_volume(volume);
	LOG_INF("Volume %u/%u", volume, CONFIG_WW_VOLUME_MAX);
}

static void next_track(void)
{
	if (!can_play()) {
		return;
	}
	if (cur_track + 1 >= pl.d.num_tracks) {
		LOG_INF("Already at the last track");
		return;
	}
	start_track(cur_track + 1, 0);
	save_resume();
}

static void seek_to(int64_t pos_ms)
{
	if (!can_play() || player_state() == WW_PLAYER_STOPPED) {
		return;
	}
	ww_player_seek(MAX(pos_ms, 0));
	save_resume();
}

static void reset_playlist(void)
{
	if (!can_play()) {
		return;
	}
	LOG_INF("Restarting playlist %s", pl.uuid);
	ww_resume_clear(pl.uuid);
	start_track(0, 0);
}

static void reset_all(void)
{
	LOG_INF("Forgetting the positions of all playlists");
	resume_clear_all();
	if (pl_loaded && tag_present && pl.d.num_tracks > 0) {
		start_track(0, 0);
	} else {
		cur_track = 0;
	}
}

static void on_tag_placed(const char *uid)
{
	struct ww_resume r;
	int ret;

	tag_present = true;

	if (pl_loaded && strcmp(pl.uuid, uid) == 0) {
		LOG_INF("Tag %s is back", uid);
		switch (player_state()) {
		case WW_PLAYER_PAUSED:
			ww_player_resume();
			return;
		case WW_PLAYER_PLAYING:
			return;
		case WW_PLAYER_STOPPED:
			break;
		}
	} else {
		if (pl_loaded) {
			save_resume();
			ww_player_stop();
		}

		ret = ww_playlist_load(&pl, uid);
		if (ret == -ENOENT) {
			LOG_INF("New tag %s, creating an empty playlist", uid);
			ret = ww_playlist_create(&pl, uid, NULL);
		}
		pl_loaded = (ret == 0);
		if (!pl_loaded) {
			LOG_ERR("Cannot load playlist for tag %s: %d", uid, ret);
			return;
		}
		LOG_INF("Tag %s: \"%s\" with %u tracks", uid, pl.d.name, pl.d.num_tracks);
	}

	if (!can_play()) {
		return;
	}

	ww_resume_load(uid, &r);
	if (r.track >= pl.d.num_tracks) {
		r.track = 0;
		r.pos_ms = 0;
	}
	start_track(r.track, r.pos_ms);
}

static void on_tag_removed(void)
{
	LOG_INF("Tag removed");
	tag_present = false;
	if (player_state() == WW_PLAYER_PLAYING) {
		ww_player_pause();
	}
	save_resume();
}

static void on_track_done(void)
{
	/* Ignore stale notifications, e.g. "next" was pressed at the very end */
	if (player_state() != WW_PLAYER_STOPPED || !pl_loaded) {
		return;
	}

	if (cur_track + 1 < pl.d.num_tracks) {
		start_track(cur_track + 1, 0);
		save_resume();
		return;
	}

	LOG_INF("Playlist %s finished", pl.uuid);
	cur_track = 0;
	ww_resume_clear(pl.uuid);
}

static void on_reload(void)
{
	char uuid[WW_UID_STR_LEN];

	if (!pl_loaded) {
		return;
	}

	strcpy(uuid, pl.uuid);
	pl_loaded = (ww_playlist_load(&pl, uuid) == 0);
	if (!pl_loaded || cur_track >= pl.d.num_tracks) {
		ww_player_stop();
		cur_track = 0;
	}
	if (pl_loaded) {
		LOG_DBG("Reloaded \"%s\" with %u tracks", pl.d.name, pl.d.num_tracks);
	}
}

static void on_button(enum ww_press press)
{
	switch (press) {
	case WW_PRESS_SHORT:
		next_track();
		break;
	case WW_PRESS_LONG:
		reset_playlist();
		break;
	case WW_PRESS_LONGLONG:
		reset_all();
		break;
	}
}

static void on_cmd(enum ww_cmd cmd, int32_t arg)
{
	switch (cmd) {
	case WW_CMD_PAUSE:
		if (player_state() == WW_PLAYER_PLAYING) {
			ww_player_pause();
			save_resume();
		}
		break;
	case WW_CMD_RESUME:
		if (player_state() == WW_PLAYER_PAUSED && can_play()) {
			ww_player_resume();
		}
		break;
	case WW_CMD_NEXT:
		next_track();
		break;
	case WW_CMD_VOLUME:
		set_volume(arg);
		break;
	case WW_CMD_SEEK:
		seek_to((int64_t)arg * 1000);
		break;
	case WW_CMD_TRACK:
		if (can_play() && arg >= 0 && arg < pl.d.num_tracks) {
			start_track(arg, 0);
			save_resume();
		}
		break;
	case WW_CMD_RESET:
		reset_playlist();
		break;
	case WW_CMD_RESET_ALL:
		reset_all();
		break;
	case WW_CMD_RELOAD:
		on_reload();
		break;
	}
}

/*
 * The button (gpio-keys, any key code): short, long and very long presses,
 * measured at release.
 */

static void button_cb(struct input_event *evt, void *user_data)
{
	static int64_t pressed_at;
	struct ww_event ev = {.type = WW_EV_BUTTON};
	int64_t duration;

	if (evt->type != INPUT_EV_KEY) {
		return;
	}
	if (evt->value) {
		pressed_at = k_uptime_get();
		return;
	}

	duration = k_uptime_get() - pressed_at;
	if (duration >= CONFIG_WW_BUTTON_LONGLONG_MS) {
		ev.press = WW_PRESS_LONGLONG;
	} else if (duration >= CONFIG_WW_BUTTON_LONG_MS) {
		ev.press = WW_PRESS_LONG;
	} else {
		ev.press = WW_PRESS_SHORT;
	}
	LOG_DBG("Button released after %lld ms", duration);
	ww_event_post(&ev);
}

INPUT_CALLBACK_DEFINE(NULL, button_cb, NULL);

/*
 * The volume potentiometer (analog-axis, 0..out-max). With some hysteresis
 * (half a step + 1 %), so a pot resting on a step boundary does not flicker.
 */

#if DT_NODE_EXISTS(POT_NODE)
static void pot_cb(struct input_event *evt, void *user_data)
{
	static int cur = -1;
	/* one volume step is POT_RANGE units */
	int pos = evt->value * CONFIG_WW_VOLUME_MAX;
	int center = cur * POT_RANGE;

	if (evt->type != INPUT_EV_ABS) {
		return;
	}
	if (cur >= 0 && abs(pos - center) <= POT_RANGE / 2 + POT_RANGE * CONFIG_WW_VOLUME_MAX / 100) {
		return;
	}

	cur = DIV_ROUND_CLOSEST(pos, POT_RANGE);
	ww_event_post_cmd(WW_CMD_VOLUME, cur);
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(POT_NODE), pot_cb, NULL);
#endif

static void player_done(int err)
{
	struct ww_event ev = {.type = WW_EV_TRACK_DONE};

	ARG_UNUSED(err);
	ww_event_post(&ev);
}

static void controller_thread(void *p1, void *p2, void *p3)
{
	struct ww_event ev;

	for (;;) {
		int ret = ww_event_get(&ev, K_SECONDS(1));

		k_mutex_lock(&ctl_lock, K_FOREVER);

		if (ret == 0) {
			switch (ev.type) {
			case WW_EV_TAG_PLACED:
				on_tag_placed(ev.tag.uid);
				break;
			case WW_EV_TAG_REMOVED:
				on_tag_removed();
				break;
			case WW_EV_BUTTON:
				on_button(ev.press);
				break;
			case WW_EV_TRACK_DONE:
				on_track_done();
				break;
			case WW_EV_CMD:
				on_cmd(ev.cmd.cmd, ev.cmd.arg);
				break;
			}
		}

		/* Persist the position now and then, in case the power goes away */
		if (player_state() == WW_PLAYER_PLAYING &&
		    k_uptime_get() - last_save > CONFIG_WW_RESUME_SAVE_INTERVAL_S * MSEC_PER_SEC) {
			save_resume();
		}

		k_mutex_unlock(&ctl_lock);
	}
}

K_THREAD_DEFINE(controller_tid, CONFIG_WW_CONTROLLER_STACK_SIZE, controller_thread, NULL, NULL,
		NULL, 5, 0, -1);

int ww_controller_init(void)
{
	int ret;

	/* The pot reports its position once it differs from 0 */
	volume = DT_NODE_EXISTS(POT_NODE) ? 0 : CONFIG_WW_VOLUME_DEFAULT;
	ww_player_set_volume(volume);
	LOG_INF("Volume %u/%u", volume, CONFIG_WW_VOLUME_MAX);

	ret = ww_player_init(player_done);
	if (ret) {
		return ret;
	}

	k_thread_name_set(controller_tid, "controller");
	k_thread_start(controller_tid);

	return 0;
}

int ww_controller_cue(const char *uuid, size_t track)
{
	struct ww_resume r = {.track = track};
	int ret = 0;

	k_mutex_lock(&ctl_lock, K_FOREVER);

	if (!pl_loaded || strcmp(pl.uuid, uuid) != 0) {
		/* Not on the box: used when its tag is placed the next time */
		ret = ww_resume_save(uuid, &r);
	} else if (track >= pl.d.num_tracks) {
		ret = -EINVAL;
	} else if (tag_present) {
		start_track(track, 0);
		save_resume();
	} else {
		/* Paused because the tag was removed: drop the paused position */
		ww_player_stop();
		cur_track = track;
		ret = ww_resume_save(uuid, &r);
	}

	k_mutex_unlock(&ctl_lock);

	return ret;
}

void ww_controller_get_status(struct ww_status *st)
{
	memset(st, 0, sizeof(*st));

	k_mutex_lock(&ctl_lock, K_FOREVER);
	st->tag_present = tag_present;
	st->volume = volume;
	if (pl_loaded) {
		strncpy(st->uid, pl.uuid, sizeof(st->uid) - 1);
		strncpy(st->name, pl.d.name, sizeof(st->name) - 1);
		st->num_tracks = pl.d.num_tracks;
		st->track = cur_track;
		if (cur_track < pl.d.num_tracks) {
			strncpy(st->track_path, pl.d.tracks[cur_track], sizeof(st->track_path) - 1);
		}
	}
	k_mutex_unlock(&ctl_lock);

	ww_player_get_status(&st->player);
}
