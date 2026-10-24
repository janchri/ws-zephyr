/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* SD card: FAT mount and card-relative paths */

#ifndef WW_STORAGE_H_
#define WW_STORAGE_H_

#include <stddef.h>

#define WW_SD_ROOT CONFIG_WW_SD_MOUNT_POINT

/* Same layout as on the ESP32/Arduino version of the box */
#define WW_MUSIC_DIR    "/Music"
#define WW_PLAYLIST_DIR WW_MUSIC_DIR "/.config"

/* Maximum length of an absolute path including the mount point */
#define WW_PATH_MAX 256

/* Build "<mount point><path>" from a card-relative path like "/Music/a.mp3" */
int ww_storage_path(char *buf, size_t len, const char *card_path);

/* mkdir -p for card-relative paths */
int ww_storage_mkdirs(const char *card_path);

/* Mount the SD card and create the directory layout */
int ww_storage_init(void);

#endif /* WW_STORAGE_H_ */
