/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host side (Linux libc) of the native_sim audio sink.
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>

/*
 * Plain C types only, the embedded side declares these in audio_native.c.
 * cmd: a shell command reading raw s16le PCM from stdin, "{rate}" and "{ch}"
 * get replaced; file: raw PCM is appended.
 */
int ww_audio_host_open(const char *cmd, const char *file, unsigned int rate, unsigned int ch);
int ww_audio_host_write(const void *buf, unsigned int len);
void ww_audio_host_close(void);

static FILE *out;
static int out_is_pipe;

static void expand(char *dst, size_t len, const char *cmd, unsigned int rate, unsigned int ch)
{
	size_t n = 0;

	while (*cmd && n + 1 < len) {
		if (strncmp(cmd, "{rate}", 6) == 0) {
			n += snprintf(&dst[n], len - n, "%u", rate);
			cmd += 6;
		} else if (strncmp(cmd, "{ch}", 4) == 0) {
			n += snprintf(&dst[n], len - n, "%u", ch);
			cmd += 4;
		} else {
			dst[n++] = *cmd++;
		}
	}
	dst[n < len ? n : len - 1] = '\0';
}

int ww_audio_host_open(const char *cmd, const char *file, unsigned int rate, unsigned int ch)
{
	char line[512];

	ww_audio_host_close();

	/* A dying player must not kill the simulation */
	signal(SIGPIPE, SIG_IGN);

	if (cmd != NULL) {
		expand(line, sizeof(line), cmd, rate, ch);
		out = popen(line, "w");
		out_is_pipe = 1;
	} else if (file != NULL) {
		out = fopen(file, "ab");
		out_is_pipe = 0;
	}

	return out ? 0 : -1;
}

int ww_audio_host_write(const void *buf, unsigned int len)
{
	if (out == NULL) {
		return -1;
	}
	if (fwrite(buf, 1, len, out) != len) {
		return -1;
	}
	fflush(out);

	return 0;
}

void ww_audio_host_close(void)
{
	if (out == NULL) {
		return;
	}
	if (out_is_pipe) {
		pclose(out);
	} else {
		fclose(out);
	}
	out = NULL;
}
