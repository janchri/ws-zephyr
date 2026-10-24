/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>

#include "storage.h"

LOG_MODULE_DECLARE(ww, CONFIG_WW_LOG_LEVEL);

static FATFS fat_fs;

static struct fs_mount_t sd_mount = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = WW_SD_ROOT,
};

int ww_storage_path(char *buf, size_t len, const char *card_path)
{
	int n = snprintf(buf, len, "%s%s%s", WW_SD_ROOT, card_path[0] == '/' ? "" : "/",
			 card_path);

	return (n < 0 || n >= len) ? -ENAMETOOLONG : 0;
}

int ww_storage_mkdirs(const char *card_path)
{
	char path[WW_PATH_MAX];
	struct fs_dirent ent;
	char *p;
	int ret;

	ret = ww_storage_path(path, sizeof(path), card_path);
	if (ret) {
		return ret;
	}

	/* Skip the mount point, then create every level */
	p = path + strlen(WW_SD_ROOT);
	while (p) {
		p = strchr(p + 1, '/');
		if (p) {
			*p = '\0';
		}
		ret = (fs_stat(path, &ent) == 0) ? 0 : fs_mkdir(path);
		if (ret) {
			LOG_ERR("mkdir %s failed: %d", path, ret);
			return ret;
		}
		if (p) {
			*p = '/';
		}
	}

	return 0;
}

int ww_storage_init(void)
{
	struct fs_statvfs st;
	int ret;

	ret = fs_mount(&sd_mount);
	if (ret) {
		LOG_ERR("Mounting SD card at %s failed: %d", WW_SD_ROOT, ret);
		return ret;
	}

	if (fs_statvfs(WW_SD_ROOT, &st) == 0) {
		LOG_INF("SD card mounted: %lu MiB total, %lu MiB free",
			(unsigned long)((uint64_t)st.f_frsize * st.f_blocks >> 20),
			(unsigned long)((uint64_t)st.f_frsize * st.f_bfree >> 20));
	}

	return ww_storage_mkdirs(WW_PLAYLIST_DIR);
}
