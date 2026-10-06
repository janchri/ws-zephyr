/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Web interface: the UI (web/index.html, built in gzipped) and a JSON API.
 *
 *   GET    /api/status                      box state
 *   POST   /api/control   {"cmd":"pause|resume|next|reset|reset_all|volume|seek|track","arg":n}
 *   GET    /api/playlists                   [{"uuid","name","tracks"}]
 *   GET    /api/playlist?uuid=U             {"uuid","name","tracks":[...],"resume":{...}}
 *   POST   /api/playlist  {"uuid":U, "name":?, "add":?, "remove":?, "track":?}
 *                         creates the playlist if needed; "track" plays that track from
 *                         its start, now or when the tag is placed the next time
 *   DELETE /api/playlist?uuid=U
 *   GET    /api/files?path=/Music           [{"name","dir","size"}]
 *   POST   /api/files?path=/Music/New       create a folder
 *   PUT    /api/files?path=/Music/a.mp3     upload, the body is the file content
 *   DELETE /api/files?path=/Music/a.mp3     delete a file or an empty folder
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>

#include "wirbelwind.h"

LOG_MODULE_DECLARE(ww, CONFIG_WW_LOG_LEVEL);

static uint16_t http_port = CONFIG_WW_HTTP_PORT;

HTTP_SERVICE_DEFINE(ww_http, NULL, &http_port, CONFIG_HTTP_SERVER_MAX_CLIENTS, 4, NULL, NULL,
		    NULL);

/* UI */

static const uint8_t index_html_gz[] = {
#include "index.html.gz.inc"
};

static struct http_resource_detail_static index_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_STATIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_encoding = "gzip",
		.content_type = "text/html",
	},
	.static_data = index_html_gz,
	.static_data_len = sizeof(index_html_gz),
};

HTTP_RESOURCE_DEFINE(index_res, ww_http, "/", &index_detail);

/*
 * All callbacks run in the single HTTP server thread and every response is
 * built and sent in one go, so one set of buffers is enough.
 */

static char out[CONFIG_WW_HTTP_RESPONSE_SIZE];
static size_t out_len;
static char body[512];
static size_t body_len;

static const struct http_header cors_headers[] = {
	{.name = "Access-Control-Allow-Origin", .value = "*"},
};

static void out_printf(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(&out[out_len], sizeof(out) - out_len, fmt, ap);
	va_end(ap);

	out_len = MIN(out_len + MAX(n, 0), sizeof(out) - 1);
}

/* JSON string; FAT names and our playlist strings never contain '"' or '\' */
static void out_str(const char *s)
{
	out_printf("\"");
	for (; *s; s++) {
		if ((unsigned char)*s >= 0x20) {
			out_printf("%c", *s);
		}
	}
	out_printf("\"");
}

static bool out_full(void)
{
	/* leave room for closing brackets */
	return out_len > sizeof(out) - 64;
}

static int respond(struct http_response_ctx *rsp, enum http_status status)
{
	rsp->status = status;
	rsp->headers = cors_headers;
	rsp->header_count = ARRAY_SIZE(cors_headers);
	rsp->body = out;
	rsp->body_len = out_len;
	rsp->final_chunk = true;
	out_len = 0;

	return 0;
}

static int respond_error(struct http_response_ctx *rsp, enum http_status status, int err)
{
	out_len = 0;
	out_printf("{\"error\":%d}", err);

	return respond(rsp, status);
}

static int respond_result(struct http_response_ctx *rsp, int ret)
{
	if (ret < 0) {
		return respond_error(rsp, ret == -ENOENT ? HTTP_404_NOT_FOUND : HTTP_400_BAD_REQUEST,
				     ret);
	}
	out_len = 0;
	out_printf("{\"ok\":true}");

	return respond(rsp, HTTP_200_OK);
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	c |= 0x20;

	return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
}

/* Value of a query parameter, URL decoded */
static bool query_get(const struct http_client_ctx *client, const char *key, char *val,
		      size_t len)
{
	const char *p = strchr((const char *)client->url_buffer, '?');
	size_t key_len = strlen(key);

	while (p && *p) {
		p++;
		if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
			size_t n = 0;

			for (p += key_len + 1; *p && *p != '&' && n + 1 < len; p++) {
				if (*p == '%' && hexval(p[1]) >= 0 && hexval(p[2]) >= 0) {
					val[n++] = hexval(p[1]) << 4 | hexval(p[2]);
					p += 2;
				} else {
					val[n++] = (*p == '+') ? ' ' : *p;
				}
			}
			val[n] = '\0';
			return true;
		}
		p = strchr(p, '&');
	}

	return false;
}

/* Card path from ?path=..., absolute and without ".." */
static bool query_path(const struct http_client_ctx *client, char *card_path, size_t len)
{
	return query_get(client, "path", card_path, len) && card_path[0] == '/' &&
	       strstr(card_path, "..") == NULL;
}

/* Collects a small request body; returns true once it is complete */
static bool collect_body(enum http_transaction_status status, const struct http_request_ctx *req)
{
	if (status == HTTP_SERVER_REQUEST_DATA_MORE || status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		size_t n = MIN(req->data_len, sizeof(body) - 1 - body_len);

		memcpy(&body[body_len], req->data, n);
		body_len += n;
	}
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		if (status != HTTP_SERVER_REQUEST_DATA_MORE) {
			body_len = 0;
		}
		return false;
	}
	body[body_len] = '\0';

	return true;
}

#define DYNAMIC_RESOURCE(_name, _path, _methods, _cb)                                              \
	static struct http_resource_detail_dynamic _name##_detail = {                              \
		.common = {                                                                        \
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,                                        \
			.bitmask_of_supported_http_methods = (_methods),                           \
			.content_type = "application/json",                                        \
		},                                                                                 \
		.cb = _cb,                                                                         \
	};                                                                                         \
	HTTP_RESOURCE_DEFINE(_name##_res, ww_http, _path, &_name##_detail)

/* Status and control */

static void out_status(void)
{
	struct ww_status st;

	ww_controller_get_status(&st);

	out_printf("{\"present\":%s,\"uuid\":", st.tag_present ? "true" : "false");
	out_str(st.uid);
	out_printf(",\"name\":");
	out_str(st.name);
	out_printf(",\"track\":%u,\"tracks\":%u,\"path\":", st.track, st.num_tracks);
	out_str(st.track_path);
	out_printf(",\"state\":\"%s\",\"pos\":%u,\"volume\":%u,\"volume_max\":%u}",
		   ww_player_state_str(st.player.state), st.player.pos_ms, st.volume,
		   CONFIG_WW_VOLUME_MAX);
}

static int status_cb(struct http_client_ctx *client, enum http_transaction_status status,
		     const struct http_request_ctx *req, struct http_response_ctx *rsp,
		     void *user_data)
{
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}
	out_status();

	return respond(rsp, HTTP_200_OK);
}

DYNAMIC_RESOURCE(status, "/api/status", BIT(HTTP_GET), status_cb);

struct control_req {
	const char *cmd;
	int32_t arg;
};

static const struct json_obj_descr control_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct control_req, cmd, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct control_req, arg, JSON_TOK_NUMBER),
};

static const struct {
	const char *name;
	enum ww_cmd cmd;
} control_cmds[] = {
	{"pause", WW_CMD_PAUSE},   {"resume", WW_CMD_RESUME},
	{"next", WW_CMD_NEXT},     {"reset", WW_CMD_RESET}, {"reset_all", WW_CMD_RESET_ALL},
	{"volume", WW_CMD_VOLUME}, {"seek", WW_CMD_SEEK},   {"track", WW_CMD_TRACK},
};

static int control_cb(struct http_client_ctx *client, enum http_transaction_status status,
		      const struct http_request_ctx *req, struct http_response_ctx *rsp,
		      void *user_data)
{
	struct control_req r = {0};

	if (!collect_body(status, req)) {
		return 0;
	}
	body_len = 0;

	if (json_obj_parse(body, strlen(body), control_descr, ARRAY_SIZE(control_descr), &r) <
		    0 ||
	    r.cmd == NULL) {
		return respond_error(rsp, HTTP_400_BAD_REQUEST, -EINVAL);
	}

	for (size_t i = 0; i < ARRAY_SIZE(control_cmds); i++) {
		if (strcmp(r.cmd, control_cmds[i].name) == 0) {
			return respond_result(rsp, ww_event_post_cmd(control_cmds[i].cmd, r.arg));
		}
	}

	return respond_error(rsp, HTTP_400_BAD_REQUEST, -EINVAL);
}

DYNAMIC_RESOURCE(control, "/api/control", BIT(HTTP_POST), control_cb);

/* Playlists */

static int list_one(const char *uuid, void *user_data)
{
	struct ww_playlist *pl = ww_playlist_scratch_lock();

	if (ww_playlist_load(pl, uuid) == 0 && !out_full()) {
		out_printf("%s{\"uuid\":", out_len > 1 ? "," : "");
		out_str(uuid);
		out_printf(",\"name\":");
		out_str(pl->d.name);
		out_printf(",\"tracks\":%u}", pl->d.num_tracks);
	}
	ww_playlist_scratch_unlock(false);

	return 0;
}

static int playlists_cb(struct http_client_ctx *client, enum http_transaction_status status,
			const struct http_request_ctx *req, struct http_response_ctx *rsp,
			void *user_data)
{
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	out_printf("[");
	ww_playlist_foreach(list_one, NULL);
	out_printf("]");

	return respond(rsp, HTTP_200_OK);
}

DYNAMIC_RESOURCE(playlists, "/api/playlists", BIT(HTTP_GET), playlists_cb);

static void out_playlist(const struct ww_playlist *pl)
{
	struct ww_resume r;

	ww_resume_load(pl->uuid, &r);

	out_printf("{\"uuid\":");
	out_str(pl->uuid);
	out_printf(",\"name\":");
	out_str(pl->d.name);
	out_printf(",\"resume\":{\"track\":%u,\"pos\":%u},\"tracks\":[", r.track, r.pos_ms);
	for (size_t i = 0; i < pl->d.num_tracks && !out_full(); i++) {
		out_printf(i ? "," : "");
		out_str(pl->d.tracks[i]);
	}
	out_printf("]}");
}

struct playlist_req {
	const char *uuid;
	const char *name;
	const char *add;
	int32_t remove;
	int32_t track;
};

static const struct json_obj_descr playlist_req_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct playlist_req, uuid, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct playlist_req, name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct playlist_req, add, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct playlist_req, remove, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct playlist_req, track, JSON_TOK_NUMBER),
};

enum { F_UUID = BIT(0), F_NAME = BIT(1), F_ADD = BIT(2), F_REMOVE = BIT(3), F_TRACK = BIT(4) };

static int playlist_post(struct ww_playlist *pl)
{
	struct playlist_req r = {0};
	int fields;
	int ret;

	fields = json_obj_parse(body, strlen(body), playlist_req_descr,
				ARRAY_SIZE(playlist_req_descr), &r);
	if (fields < 0 || !(fields & F_UUID)) {
		return -EINVAL;
	}

	ret = ww_playlist_load(pl, r.uuid);
	if (ret == -ENOENT) {
		ret = ww_playlist_create(pl, r.uuid, (fields & F_NAME) ? r.name : NULL);
	}
	if (ret == 0 && (fields & F_NAME)) {
		ret = ww_playlist_set_name(pl, r.name);
	}
	if (ret == 0 && (fields & F_ADD)) {
		ret = ww_playlist_add(pl, r.add);
		ret = MIN(ret, 0);
	}
	if (ret == 0 && (fields & F_REMOVE)) {
		ret = ww_playlist_remove_track(pl, r.remove);
	}
	if (ret == 0 && (fields & F_TRACK)) {
		ret = (r.track < 0) ? -EINVAL : ww_controller_cue(pl->uuid, r.track);
	}

	return ret;
}

static int playlist_cb(struct http_client_ctx *client, enum http_transaction_status status,
		       const struct http_request_ctx *req, struct http_response_ctx *rsp,
		       void *user_data)
{
	char uuid[WW_UID_STR_LEN];
	struct ww_playlist *pl;
	bool modified = false;
	int ret;

	if (client->method == HTTP_POST) {
		if (!collect_body(status, req)) {
			return 0;
		}
		body_len = 0;
	} else if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	} else if (!query_get(client, "uuid", uuid, sizeof(uuid))) {
		return respond_error(rsp, HTTP_400_BAD_REQUEST, -EINVAL);
	}

	pl = ww_playlist_scratch_lock();

	switch (client->method) {
	case HTTP_GET:
		ret = ww_playlist_load(pl, uuid);
		break;
	case HTTP_POST:
		ret = playlist_post(pl);
		modified = true;
		break;
	case HTTP_DELETE:
		ret = ww_playlist_delete(uuid);
		if (ret == 0) {
			ww_resume_clear(uuid);
			modified = true;
		}
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	if (ret == 0 && client->method != HTTP_DELETE) {
		out_playlist(pl);
		ret = respond(rsp, HTTP_200_OK);
	} else {
		ret = respond_result(rsp, ret);
	}
	ww_playlist_scratch_unlock(modified);

	return ret;
}

DYNAMIC_RESOURCE(playlist, "/api/playlist", BIT(HTTP_GET) | BIT(HTTP_POST) | BIT(HTTP_DELETE),
		 playlist_cb);

/* Files */

static struct {
	struct fs_file_t file;
	bool open;
	int err;
	size_t size;
	char path[WW_PATH_MAX];
} upload;

static int list_files(const char *path, struct http_response_ctx *rsp)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;
	bool first = true;
	int ret;

	fs_dir_t_init(&dir);
	ret = fs_opendir(&dir, path);
	if (ret) {
		return respond_result(rsp, ret);
	}

	out_printf("[");
	while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0' && !out_full()) {
		out_printf(first ? "{\"name\":" : ",{\"name\":");
		out_str(ent.name);
		out_printf(",\"dir\":%s,\"size\":%u}",
			   ent.type == FS_DIR_ENTRY_DIR ? "true" : "false", (unsigned int)ent.size);
		first = false;
	}
	fs_closedir(&dir);
	out_printf("]");

	return respond(rsp, HTTP_200_OK);
}

static void upload_close(bool keep)
{
	if (upload.open) {
		fs_close(&upload.file);
		upload.open = false;
		if (!keep) {
			fs_unlink(upload.path);
		}
	}
}

static int upload_data(struct http_client_ctx *client, enum http_transaction_status status,
		       const struct http_request_ctx *req, struct http_response_ctx *rsp)
{
	char card_path[WW_PATH_MAX];
	ssize_t n;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		LOG_WRN("Upload of %s aborted", upload.path);
		upload_close(false);
		upload.err = 0;
		return 0;
	}
	if (status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		upload.err = 0;
		return 0;
	}

	if (!upload.open && upload.err == 0) {
		upload.size = 0;
		upload.err = query_path(client, card_path, sizeof(card_path)) ? 0 : -EINVAL;
		if (upload.err == 0) {
			upload.err = ww_storage_path(upload.path, sizeof(upload.path), card_path);
		}
		if (upload.err == 0) {
			fs_file_t_init(&upload.file);
			upload.err = fs_open(&upload.file, upload.path,
					     FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
			upload.open = (upload.err == 0);
		}
		if (upload.err == 0) {
			LOG_INF("Upload %s", card_path);
		}
	}

	if (upload.open && req->data_len > 0) {
		n = fs_write(&upload.file, req->data, req->data_len);
		if (n != req->data_len) {
			upload.err = n < 0 ? n : -ENOSPC;
			upload_close(false);
		}
		upload.size += req->data_len;
	}

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	upload_close(true);
	if (upload.err) {
		return respond_result(rsp, upload.err);
	}
	LOG_INF("Upload done, %u bytes", upload.size);

	return respond_result(rsp, 0);
}

static int files_cb(struct http_client_ctx *client, enum http_transaction_status status,
		    const struct http_request_ctx *req, struct http_response_ctx *rsp,
		    void *user_data)
{
	char card_path[WW_PATH_MAX];
	char path[WW_PATH_MAX];

	if (client->method == HTTP_PUT) {
		return upload_data(client, status, req, rsp);
	}
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	if (!query_path(client, card_path, sizeof(card_path)) ||
	    ww_storage_path(path, sizeof(path), card_path)) {
		return respond_error(rsp, HTTP_400_BAD_REQUEST, -EINVAL);
	}

	switch (client->method) {
	case HTTP_GET:
		return list_files(path, rsp);
	case HTTP_POST:
		return respond_result(rsp, ww_storage_mkdirs(card_path));
	case HTTP_DELETE:
		return respond_result(rsp, fs_unlink(path));
	default:
		return respond_error(rsp, HTTP_400_BAD_REQUEST, -ENOTSUP);
	}
}

DYNAMIC_RESOURCE(files, "/api/files",
		 BIT(HTTP_GET) | BIT(HTTP_POST) | BIT(HTTP_PUT) | BIT(HTTP_DELETE), files_cb);

int ww_http_init(void)
{
	int ret = http_server_start();

	if (ret) {
		LOG_ERR("HTTP server start failed: %d", ret);
		return ret;
	}
	LOG_INF("Web interface on port %u", http_port);

	return 0;
}
