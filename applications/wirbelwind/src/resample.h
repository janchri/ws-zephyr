/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Sample rate conversion to a fixed output rate, interleaved 16 bit stereo
 * out. Linear interpolation: good enough for speech and music on a small
 * speaker, cheap enough for the ESP32. Mono input is duplicated.
 */

#ifndef WW_RESAMPLE_H_
#define WW_RESAMPLE_H_

#include <stddef.h>
#include <stdint.h>

struct ww_resampler {
	uint32_t step;  /* input frames per output frame, Q16 */
	uint32_t phase; /* position between prev and the next input frame, Q16 */
	int16_t prev[2];
	uint8_t channels;
};

/* Called for every output frame; a non-zero return stops the conversion */
typedef int (*ww_resample_out_t)(int16_t left, int16_t right);

void ww_resample_init(struct ww_resampler *rs, uint32_t in_rate, uint8_t in_channels,
		      uint32_t out_rate);

int ww_resample(struct ww_resampler *rs, const int16_t *in, size_t frames, ww_resample_out_t out);

#endif /* WW_RESAMPLE_H_ */
