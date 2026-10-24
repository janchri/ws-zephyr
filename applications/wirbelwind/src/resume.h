/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Where each playlist continues, stored in internal flash (settings),
 * independent of the SD card. */

#ifndef WW_RESUME_H_
#define WW_RESUME_H_

#include <stdint.h>

struct ww_resume {
	uint16_t track;
	uint32_t pos_ms;
};

void ww_resume_load(const char *uuid, struct ww_resume *r);
int ww_resume_save(const char *uuid, const struct ww_resume *r);
int ww_resume_clear(const char *uuid);

/* Forget the positions of all playlists */
void ww_resume_clear_all(void);

#endif /* WW_RESUME_H_ */
