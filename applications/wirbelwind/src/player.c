/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MP3 player thread: SD card -> minimp3 -> volume -> audio sink.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include <minimp3.h>

#include "wirbelwind.h"

LOG_MODULE_DECLARE(ww, CONFIG_WW_LOG_LEVEL);

enum player_op {
	OP_PLAY,
	OP_PAUSE,
	OP_RESUME,
	OP_STOP,
	OP_SEEK,
};

struct player_cmd {
	enum player_op op;
	uint32_t pos_ms;
	char path[WW_PATH_MAX];
};

K_MSGQ_DEFINE(player_cmd_q, sizeof(struct player_cmd), 1, 4);
static K_SEM_DEFINE(player_cmd_done, 0, 1);
static K_MUTEX_DEFINE(player_api_lock);
static int player_cmd_ret;

static ww_player_done_cb_t done_cb;

/* Decoder state, only touched by the player thread */
static struct fs_file_t file;
static bool file_open;
static bool eof;
static uint8_t inbuf[CONFIG_WW_PLAYER_INBUF_SIZE];
static size_t in_len;
static size_t in_pos;
static mp3dec_t mp3d;
static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static uint64_t pos_frames;
static bool sink_running;
static char cur_path[WW_PATH_MAX];

/* Shared with other threads */
static struct k_spinlock status_lock;
static struct ww_player_status status;
static atomic_t gain_q15 = ATOMIC_INIT(0);

static void set_state(enum ww_player_state state)
{
	K_SPINLOCK(&status_lock) {
		status.state = state;
	}
}

static void update_status(const mp3dec_frame_info_t *info)
{
	K_SPINLOCK(&status_lock) {
		status.rate = info->hz;
		status.channels = info->channels;
		status.bitrate_kbps = info->bitrate_kbps;
		status.pos_ms = info->hz ? (uint32_t)(pos_frames * 1000 / info->hz) : 0;
	}
}

static void refill(void)
{
	ssize_t n;

	if (in_pos > 0) {
		memmove(inbuf, &inbuf[in_pos], in_len - in_pos);
		in_len -= in_pos;
		in_pos = 0;
	}
	if (eof || in_len == sizeof(inbuf)) {
		return;
	}

	n = fs_read(&file, &inbuf[in_len], sizeof(inbuf) - in_len);
	if (n <= 0) {
		if (n < 0) {
			LOG_ERR("Read error %d", (int)n);
		}
		eof = true;
		return;
	}
	in_len += n;
}

/* Returns samples per channel of the next frame, 0 at the end. pcm_out may be NULL to skip. */
static int decode_frame(int16_t *pcm_out, mp3dec_frame_info_t *info)
{
	int samples;

	for (;;) {
		if (in_len - in_pos < sizeof(inbuf) / 2 && !eof) {
			refill();
		}
		if (in_pos == in_len) {
			return 0;
		}

		samples = mp3dec_decode_frame(&mp3d, &inbuf[in_pos], in_len - in_pos, pcm_out,
					      info);
		if (info->frame_bytes == 0) {
			/* No frame in the buffer: get more data or give up */
			if (eof) {
				return 0;
			}
			if (in_pos == 0 && in_len == sizeof(inbuf)) {
				in_pos = in_len;
			}
			refill();
			continue;
		}

		in_pos += info->frame_bytes;
		if (samples > 0) {
			return samples;
		}
		/* frame_bytes > 0 && samples == 0: skipped garbage/ID3 data */
	}
}

static void close_file(void)
{
	if (file_open) {
		fs_close(&file);
		file_open = false;
	}
}

static void stop_sink(void)
{
	if (sink_running) {
		audio_sink_stop();
		sink_running = false;
	}
}

static int skip_id3v2(void)
{
	uint8_t hdr[10];
	uint32_t size;

	if (fs_read(&file, hdr, sizeof(hdr)) != sizeof(hdr) || memcmp(hdr, "ID3", 3) != 0) {
		return fs_seek(&file, 0, FS_SEEK_SET);
	}

	size = ((hdr[6] & 0x7f) << 21) | ((hdr[7] & 0x7f) << 14) | ((hdr[8] & 0x7f) << 7) |
	       (hdr[9] & 0x7f);
	size += sizeof(hdr);
	if (hdr[5] & 0x10) {
		size += 10; /* footer */
	}

	return fs_seek(&file, size, FS_SEEK_SET);
}

static int open_track(const char *path)
{
	int ret;

	close_file();
	stop_sink();

	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_READ);
	if (ret) {
		LOG_ERR("Cannot open %s: %d", path, ret);
		return ret;
	}
	file_open = true;

	ret = skip_id3v2();
	if (ret) {
		close_file();
		return ret;
	}

	mp3dec_init(&mp3d);
	in_len = 0;
	in_pos = 0;
	eof = false;
	pos_frames = 0;
	strncpy(cur_path, path, sizeof(cur_path) - 1);

	return 0;
}

/* Skip forward to pos_ms without decoding the audio data */
static void skip_to(uint32_t pos_ms)
{
	mp3dec_frame_info_t info = {0};
	int samples;

	while ((samples = decode_frame(NULL, &info)) > 0) {
		pos_frames += samples;
		if (pos_frames * 1000 / info.hz >= pos_ms) {
			break;
		}
	}
	/* The bit reservoir is invalid after skipping */
	mp3dec_init(&mp3d);
	update_status(&info);
}

static void apply_volume(int16_t *buf, size_t n)
{
	int32_t gain = atomic_get(&gain_q15);

	for (size_t i = 0; i < n; i++) {
		buf[i] = (int16_t)(((int32_t)buf[i] * gain) >> 15);
	}
}

static void play_frame(void)
{
	mp3dec_frame_info_t info;
	int samples = decode_frame(pcm, &info);

	if (samples == 0) {
		LOG_INF("End of track");
		close_file();
		stop_sink();
		set_state(WW_PLAYER_STOPPED);
		if (done_cb) {
			done_cb(0);
		}
		return;
	}

	if (!sink_running) {
		if (audio_sink_start(info.hz, info.channels) == 0) {
			sink_running = true;
		}
	}

	apply_volume(pcm, samples * info.channels);
	audio_sink_write(pcm, samples);

	pos_frames += samples;
	update_status(&info);
}

static int handle_cmd(struct player_cmd *cmd)
{
	struct ww_player_status st;
	int ret;

	ww_player_get_status(&st);

	switch (cmd->op) {
	case OP_PLAY:
		ret = open_track(cmd->path);
		if (ret) {
			set_state(WW_PLAYER_STOPPED);
			return ret;
		}
		LOG_INF("Playing %s from %u ms", cmd->path, cmd->pos_ms);
		if (cmd->pos_ms) {
			skip_to(cmd->pos_ms);
		}
		set_state(WW_PLAYER_PLAYING);
		return 0;

	case OP_PAUSE:
		if (st.state == WW_PLAYER_PLAYING) {
			stop_sink();
			set_state(WW_PLAYER_PAUSED);
		}
		return 0;

	case OP_RESUME:
		if (st.state == WW_PLAYER_PAUSED) {
			set_state(WW_PLAYER_PLAYING);
		}
		return 0;

	case OP_STOP:
		close_file();
		stop_sink();
		set_state(WW_PLAYER_STOPPED);
		return 0;

	case OP_SEEK:
		if (!file_open) {
			return -EINVAL;
		}
		stop_sink();
		if (cmd->pos_ms < st.pos_ms) {
			/* No index in MP3 files: rewind and skip from the start */
			strcpy(cmd->path, cur_path);
			ret = open_track(cmd->path);
			if (ret) {
				set_state(WW_PLAYER_STOPPED);
				return ret;
			}
		}
		skip_to(cmd->pos_ms);
		return 0;
	}

	return -EINVAL;
}

static void player_thread(void *p1, void *p2, void *p3)
{
	struct player_cmd cmd;
	struct ww_player_status st;

	for (;;) {
		ww_player_get_status(&st);
		if (k_msgq_get(&player_cmd_q, &cmd,
			       st.state == WW_PLAYER_PLAYING ? K_NO_WAIT : K_FOREVER) == 0) {
			player_cmd_ret = handle_cmd(&cmd);
			k_sem_give(&player_cmd_done);
			continue;
		}

		play_frame();
	}
}

K_THREAD_DEFINE(player_tid, CONFIG_WW_PLAYER_STACK_SIZE, player_thread, NULL, NULL, NULL,
		CONFIG_WW_PLAYER_PRIORITY, K_FP_REGS, -1);

static int send_cmd(struct player_cmd *cmd)
{
	int ret;

	k_mutex_lock(&player_api_lock, K_FOREVER);
	k_msgq_put(&player_cmd_q, cmd, K_FOREVER);
	k_sem_take(&player_cmd_done, K_FOREVER);
	ret = player_cmd_ret;
	k_mutex_unlock(&player_api_lock);

	return ret;
}

int ww_player_init(ww_player_done_cb_t cb)
{
	done_cb = cb;
	k_thread_name_set(player_tid, "player");
	k_thread_start(player_tid);

	return audio_sink_init();
}

int ww_player_play(const char *card_path, uint32_t start_ms)
{
	struct player_cmd cmd = {.op = OP_PLAY, .pos_ms = start_ms};
	int ret;

	ret = ww_storage_path(cmd.path, sizeof(cmd.path), card_path);
	if (ret) {
		return ret;
	}

	return send_cmd(&cmd);
}

void ww_player_pause(void)
{
	struct player_cmd cmd = {.op = OP_PAUSE};

	send_cmd(&cmd);
}

void ww_player_resume(void)
{
	struct player_cmd cmd = {.op = OP_RESUME};

	send_cmd(&cmd);
}

void ww_player_stop(void)
{
	struct player_cmd cmd = {.op = OP_STOP};

	send_cmd(&cmd);
}

int ww_player_seek(uint32_t pos_ms)
{
	struct player_cmd cmd = {.op = OP_SEEK, .pos_ms = pos_ms};

	return send_cmd(&cmd);
}

void ww_player_set_volume(uint8_t volume)
{
	/* 2 dB per step below the maximum, 0 is mute */
	int32_t gain = 0;

	if (volume > 0) {
		gain = 32767;
		for (int i = volume; i < CONFIG_WW_VOLUME_MAX; i++) {
			gain = gain * 26029 / 32768;
		}
	}
	atomic_set(&gain_q15, gain);
}

void ww_player_get_status(struct ww_player_status *st)
{
	K_SPINLOCK(&status_lock) {
		*st = status;
	}
}

const char *ww_player_state_str(enum ww_player_state state)
{
	static const char *const names[] = {
		[WW_PLAYER_STOPPED] = "stopped",
		[WW_PLAYER_PLAYING] = "playing",
		[WW_PLAYER_PAUSED] = "paused",
	};

	return names[state];
}
