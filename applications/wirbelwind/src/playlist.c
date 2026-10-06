/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <zephyr/data/json.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>

#include "wirbelwind.h"

LOG_MODULE_DECLARE(ww, CONFIG_WW_LOG_LEVEL);

#define PLAYLIST_PREFIX "playlist_"

static const struct json_obj_descr playlist_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct ww_playlist_data, uuid, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ww_playlist_data, name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_ARRAY(struct ww_playlist_data, tracks, CONFIG_WW_PLAYLIST_MAX_TRACKS,
			     num_tracks, JSON_TOK_STRING),
};

bool ww_playlist_uuid_valid(const char *uuid)
{
	size_t len = strlen(uuid);

	if (len == 0 || len >= WW_UID_STR_LEN) {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		if (!isalnum((unsigned char)uuid[i]) && uuid[i] != '-' && uuid[i] != '_') {
			return false;
		}
	}

	return true;
}

/* The JSON parser does not unescape, so keep strings free of escapes */
static bool string_valid(const char *s)
{
	return strpbrk(s, "\"\\") == NULL;
}

static int playlist_file(char *buf, size_t len, const char *uuid, const char *suffix)
{
	char card_path[64];

	snprintf(card_path, sizeof(card_path), WW_PLAYLIST_DIR "/" PLAYLIST_PREFIX "%s%s", uuid,
		 suffix);

	return ww_storage_path(buf, len, card_path);
}

static const char *pl_strdup(struct ww_playlist *pl, const char *s)
{
	size_t len = strlen(s) + 1;
	char *dst = &pl->buf[pl->used];

	if (pl->used + len > sizeof(pl->buf)) {
		return NULL;
	}
	memcpy(dst, s, len);
	pl->used += len;

	return dst;
}

static void playlist_init(struct ww_playlist *pl, const char *uuid)
{
	memset(&pl->d, 0, sizeof(pl->d));
	pl->used = 0;
	strncpy(pl->uuid, uuid, sizeof(pl->uuid) - 1);
	pl->uuid[sizeof(pl->uuid) - 1] = '\0';
}

int ww_playlist_load(struct ww_playlist *pl, const char *uuid)
{
	char path[WW_PATH_MAX];
	struct fs_dirent ent;
	struct fs_file_t file;
	ssize_t len;
	int ret;

	if (!ww_playlist_uuid_valid(uuid)) {
		return -EINVAL;
	}

	playlist_init(pl, uuid);
	playlist_file(path, sizeof(path), uuid, "");

	/* Unknown tags are normal, don't let fs_open() log an error */
	if (fs_stat(path, &ent) != 0) {
		return -ENOENT;
	}

	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_READ);
	if (ret) {
		return ret;
	}
	len = fs_read(&file, pl->buf, sizeof(pl->buf) - 1);
	fs_close(&file);
	if (len < 0) {
		return len;
	}
	if (len == sizeof(pl->buf) - 1) {
		LOG_ERR("Playlist %s too large", uuid);
		return -EFBIG;
	}
	pl->buf[len] = '\0';
	pl->used = len + 1;

	ret = json_obj_parse(pl->buf, len, playlist_descr, ARRAY_SIZE(playlist_descr), &pl->d);
	if (ret < 0) {
		LOG_ERR("Playlist %s is corrupt: %d", uuid, ret);
		pl->used = 0;
		memset(&pl->d, 0, sizeof(pl->d));
		return ret;
	}

	/* The file name is authoritative for the uuid */
	pl->d.uuid = pl->uuid;
	if (!(ret & BIT(1)) || pl->d.name == NULL) {
		pl->d.name = pl->uuid;
	}

	return 0;
}

static int append_to_file(const char *bytes, size_t len, void *data)
{
	ssize_t ret = fs_write(data, bytes, len);

	return (ret == len) ? 0 : -EIO;
}

static int playlist_save(const struct ww_playlist *pl)
{
	char path[WW_PATH_MAX];
	char tmp_path[WW_PATH_MAX];
	struct fs_dirent ent;
	struct fs_file_t file;
	int ret;

	playlist_file(path, sizeof(path), pl->uuid, "");
	playlist_file(tmp_path, sizeof(tmp_path), pl->uuid, ".tmp");

	fs_file_t_init(&file);
	ret = fs_open(&file, tmp_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret) {
		LOG_ERR("Cannot write %s: %d", tmp_path, ret);
		return ret;
	}
	ret = json_obj_encode(playlist_descr, ARRAY_SIZE(playlist_descr), &pl->d, append_to_file,
			      &file);
	fs_close(&file);
	if (ret) {
		fs_unlink(tmp_path);
		return ret;
	}

	/* FatFs refuses to rename onto an existing file */
	if (fs_stat(path, &ent) == 0) {
		fs_unlink(path);
	}

	return fs_rename(tmp_path, path);
}

int ww_playlist_create(struct ww_playlist *pl, const char *uuid, const char *name)
{
	if (!ww_playlist_uuid_valid(uuid) || (name && !string_valid(name))) {
		return -EINVAL;
	}

	playlist_init(pl, uuid);
	pl->d.uuid = pl->uuid;
	pl->d.name = pl_strdup(pl, name ? name : uuid);

	return playlist_save(pl);
}

static int commit(struct ww_playlist *pl)
{
	char uuid[WW_UID_STR_LEN];
	int ret;

	ret = playlist_save(pl);
	if (ret) {
		return ret;
	}

	/* Reload to compact the string buffer */
	strcpy(uuid, pl->uuid);

	return ww_playlist_load(pl, uuid);
}

int ww_playlist_set_name(struct ww_playlist *pl, const char *name)
{
	if (!string_valid(name)) {
		return -EINVAL;
	}
	pl->d.name = pl_strdup(pl, name);
	if (pl->d.name == NULL) {
		return -ENOMEM;
	}

	return commit(pl);
}

static bool is_mp3(const char *name)
{
	size_t len = strlen(name);

	return len > 4 && strcasecmp(&name[len - 4], ".mp3") == 0;
}

static int add_track(struct ww_playlist *pl, const char *card_path)
{
	const char *s;

	if (pl->d.num_tracks >= ARRAY_SIZE(pl->d.tracks)) {
		return -ENOSPC;
	}
	s = pl_strdup(pl, card_path);
	if (s == NULL) {
		return -ENOMEM;
	}
	pl->d.tracks[pl->d.num_tracks++] = s;

	return 0;
}

static int cmp_str(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int add_dir(struct ww_playlist *pl, const char *dir_path, const char *card_path)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;
	char track[WW_PATH_MAX];
	size_t first = pl->d.num_tracks;
	int ret;

	fs_dir_t_init(&dir);
	ret = fs_opendir(&dir, dir_path);
	if (ret) {
		return ret;
	}

	while ((ret = fs_readdir(&dir, &ent)) == 0 && ent.name[0] != '\0') {
		if (ent.type != FS_DIR_ENTRY_FILE || !is_mp3(ent.name)) {
			continue;
		}
		if (snprintf(track, sizeof(track), "%s/%s", card_path, ent.name) >= sizeof(track) ||
		    !string_valid(track)) {
			continue;
		}
		ret = add_track(pl, track);
		if (ret) {
			break;
		}
	}
	fs_closedir(&dir);

	qsort(&pl->d.tracks[first], pl->d.num_tracks - first, sizeof(pl->d.tracks[0]), cmp_str);

	return ret;
}

int ww_playlist_add(struct ww_playlist *pl, const char *card_path)
{
	char path[WW_PATH_MAX];
	struct fs_dirent ent;
	size_t before = pl->d.num_tracks;
	int ret;

	if (!string_valid(card_path) || card_path[0] != '/') {
		return -EINVAL;
	}

	ret = ww_storage_path(path, sizeof(path), card_path);
	if (ret) {
		return ret;
	}
	ret = fs_stat(path, &ent);
	if (ret) {
		return ret;
	}

	if (ent.type == FS_DIR_ENTRY_DIR) {
		ret = add_dir(pl, path, card_path);
	} else if (is_mp3(card_path)) {
		ret = add_track(pl, card_path);
	} else {
		ret = -ENOTSUP;
	}

	if (pl->d.num_tracks == before) {
		return ret;
	}
	if (ret) {
		LOG_WRN("Playlist full, not all tracks added (%d)", ret);
	}

	ret = commit(pl);

	return ret ? ret : (int)(pl->d.num_tracks - before);
}

int ww_playlist_remove_track(struct ww_playlist *pl, size_t idx)
{
	if (idx >= pl->d.num_tracks) {
		return -EINVAL;
	}

	memmove(&pl->d.tracks[idx], &pl->d.tracks[idx + 1],
		(pl->d.num_tracks - idx - 1) * sizeof(pl->d.tracks[0]));
	pl->d.num_tracks--;

	return commit(pl);
}

int ww_playlist_delete(const char *uuid)
{
	char path[WW_PATH_MAX];

	if (!ww_playlist_uuid_valid(uuid)) {
		return -EINVAL;
	}
	playlist_file(path, sizeof(path), uuid, "");

	return fs_unlink(path);
}

int ww_playlist_foreach(ww_playlist_cb_t cb, void *user_data)
{
	char path[WW_PATH_MAX];
	struct fs_dir_t dir;
	struct fs_dirent ent;
	int ret;

	ww_storage_path(path, sizeof(path), WW_PLAYLIST_DIR);

	fs_dir_t_init(&dir);
	ret = fs_opendir(&dir, path);
	if (ret) {
		return ret;
	}

	while ((ret = fs_readdir(&dir, &ent)) == 0 && ent.name[0] != '\0') {
		const char *uuid = ent.name + strlen(PLAYLIST_PREFIX);

		if (ent.type != FS_DIR_ENTRY_FILE ||
		    strncmp(ent.name, PLAYLIST_PREFIX, strlen(PLAYLIST_PREFIX)) != 0 ||
		    !ww_playlist_uuid_valid(uuid)) {
			continue;
		}
		if (cb(uuid, user_data)) {
			break;
		}
	}
	fs_closedir(&dir);

	return ret;
}

static K_MUTEX_DEFINE(scratch_lock);
static struct ww_playlist scratch;

struct ww_playlist *ww_playlist_scratch_lock(void)
{
	k_mutex_lock(&scratch_lock, K_FOREVER);

	return &scratch;
}

void ww_playlist_scratch_unlock(bool modified)
{
	if (modified) {
		ww_event_post_cmd(WW_CMD_RELOAD, 0);
	}
	k_mutex_unlock(&scratch_lock);
}
