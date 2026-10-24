/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Where each playlist continues: "ww/pos/<uuid>" in the settings subsystem
 * (NVS in internal flash).
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/settings/settings.h>

#include "events.h"
#include "resume.h"

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

void ww_resume_clear_all(void)
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
