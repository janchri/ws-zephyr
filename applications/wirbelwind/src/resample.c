/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "resample.h"

#define Q16_ONE (1U << 16)

void ww_resample_init(struct ww_resampler *rs, uint32_t in_rate, uint8_t in_channels,
		      uint32_t out_rate)
{
	rs->step = ((uint64_t)in_rate << 16) / out_rate;
	rs->phase = 0;
	rs->prev[0] = 0;
	rs->prev[1] = 0;
	rs->channels = in_channels;
}

static int16_t lerp(int16_t a, int16_t b, uint32_t frac_q16)
{
	/* 17 bit difference x 14 bit fraction fits into 32 bit */
	return a + (((int32_t)(b - a) * (int32_t)(frac_q16 >> 2)) >> 14);
}

int ww_resample(struct ww_resampler *rs, const int16_t *in, size_t frames, ww_resample_out_t out)
{
	int ret;

	for (size_t i = 0; i < frames; i++) {
		int16_t l = in[i * rs->channels];
		int16_t r = (rs->channels > 1) ? in[i * rs->channels + 1] : l;

		if (rs->step == Q16_ONE) {
			ret = out(l, r);
			if (ret) {
				return ret;
			}
			continue;
		}

		/* Output frames lying between the previous and this input frame */
		while (rs->phase < Q16_ONE) {
			ret = out(lerp(rs->prev[0], l, rs->phase), lerp(rs->prev[1], r, rs->phase));
			if (ret) {
				return ret;
			}
			rs->phase += rs->step;
		}
		rs->phase -= Q16_ONE;
		rs->prev[0] = l;
		rs->prev[1] = r;
	}

	return 0;
}
