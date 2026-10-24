/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Audio output, implemented per platform (native/audio.c, ...).
 * Interleaved signed 16 bit PCM. */

#ifndef WW_AUDIO_H_
#define WW_AUDIO_H_

#include <stddef.h>
#include <stdint.h>

int audio_sink_init(void);
/* (Re)configure and start the output, called before the first write after a stop */
int audio_sink_start(uint32_t rate, uint8_t channels);
/* Blocks until the data is accepted, i.e. paces the caller to real time */
int audio_sink_write(const int16_t *samples, size_t frames);
/* Stop the output (pause or end of track) */
void audio_sink_stop(void);

#endif /* WW_AUDIO_H_ */
