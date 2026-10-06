/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * native_sim audio output. Command line options:
 *   --audio_cmd="aplay -q -f S16_LE -r {rate} -c {ch}"   play on the host
 *   --audio_file=out.raw                                append raw PCM to a file
 * Without either option the audio is discarded (but still paced in real time).
 */

#include <zephyr/logging/log.h>

#include <cmdline.h>
#include <posix_native_task.h>

#include "wirbelwind.h"

LOG_MODULE_DECLARE(ww, CONFIG_WW_LOG_LEVEL);

/* Host side, see audio_native_bottom.c */
int ww_audio_host_open(const char *cmd, const char *file, unsigned int rate, unsigned int ch);
int ww_audio_host_write(const void *buf, unsigned int len);
void ww_audio_host_close(void);

/* There is no hardware clock: keep the producer at real time */
struct audio_pacer {
	int64_t t0;
	uint64_t frames;
	uint32_t rate;
};

#define AUDIO_PACER_LEAD_MS 100

static void audio_pacer_start(struct audio_pacer *p, uint32_t rate)
{
	p->t0 = k_uptime_get();
	p->frames = 0;
	p->rate = rate;
}

static void audio_pacer_wait(struct audio_pacer *p, size_t frames)
{
	int64_t ahead;

	p->frames += frames;
	ahead = (int64_t)(p->frames * 1000 / p->rate) - (k_uptime_get() - p->t0);
	if (ahead > AUDIO_PACER_LEAD_MS) {
		k_msleep(ahead - AUDIO_PACER_LEAD_MS);
	} else if (ahead < -AUDIO_PACER_LEAD_MS) {
		/* Fell behind (e.g. slow SD card), don't try to catch up in a burst */
		audio_pacer_start(p, p->rate);
	}
}

static const char *audio_cmd;
static const char *audio_file;
static struct audio_pacer pacer;
static bool host_open;
static uint8_t num_channels;

static void audio_host_options(void)
{
	static struct args_struct_t options[] = {
		{.option = "audio_cmd",
		 .name = "cmd",
		 .type = 's',
		 .dest = (void *)&audio_cmd,
		 .descript = "Host command that plays raw s16le PCM from stdin, "
			     "{rate} and {ch} are replaced, e.g. "
			     "\"aplay -q -f S16_LE -r {rate} -c {ch}\""},
		{.option = "audio_file",
		 .name = "path",
		 .type = 's',
		 .dest = (void *)&audio_file,
		 .descript = "Append the raw s16le PCM output to this file"},
		ARG_TABLE_ENDMARKER,
	};

	native_add_command_line_opts(options);
}

NATIVE_TASK(audio_host_options, PRE_BOOT_1, 10);

int audio_sink_init(void)
{
	if (audio_cmd) {
		LOG_INF("Audio output: %s", audio_cmd);
	} else if (audio_file) {
		LOG_INF("Audio output: file %s", audio_file);
	} else {
		LOG_INF("Audio output: none (use --audio_cmd or --audio_file)");
	}

	return 0;
}

int audio_sink_start(uint32_t rate, uint8_t channels)
{
	audio_pacer_start(&pacer, rate);
	num_channels = channels;

	if (audio_cmd == NULL && audio_file == NULL) {
		return 0;
	}
	if (ww_audio_host_open(audio_cmd, audio_file, rate, channels)) {
		LOG_ERR("Cannot open host audio output");
		return -EIO;
	}
	host_open = true;

	return 0;
}

int audio_sink_write(const int16_t *samples, size_t frames)
{
	if (host_open) {
		ww_audio_host_write(samples, frames * num_channels * sizeof(int16_t));
	}
	audio_pacer_wait(&pacer, frames);

	return 0;
}

void audio_sink_stop(void)
{
	if (host_open) {
		ww_audio_host_close();
		host_open = false;
	}
}
