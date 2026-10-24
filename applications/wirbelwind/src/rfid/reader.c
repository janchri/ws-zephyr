/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tag reader: polls the MFRC522 and posts WW_EV_TAG_PLACED / _REMOVED.
 * A card has to be missing for a few polls in a row before it counts as
 * removed, single failed reads happen now and then.
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "events.h"
#include "mfrc522.h"

LOG_MODULE_DECLARE(ww, CONFIG_WW_LOG_LEVEL);

#define RFID_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(nxp_mfrc522)

BUILD_ASSERT(WW_UID_STR_LEN >= 2 * MFRC522_UID_MAX + 1);

static void uid_to_str(const uint8_t *uid, size_t len, char *str)
{
	/* Lower case hex, like the Arduino version: playlists are named after it */
	for (size_t i = 0; i < len; i++) {
		sprintf(&str[2 * i], "%02x", uid[i]);
	}
	str[2 * len] = '\0';
}

static void reader_thread(void *p1, void *p2, void *p3)
{
	const struct device *dev = DEVICE_DT_GET(RFID_NODE);
	struct ww_event ev;
	char current[WW_UID_STR_LEN] = "";
	char uid_str[WW_UID_STR_LEN];
	uint8_t uid[MFRC522_UID_MAX];
	size_t len;
	int misses = 0;
	int ret;

	if (!device_is_ready(dev)) {
		LOG_ERR("RFID reader not available, tags only via the shell");
		return;
	}

	for (;;) {
		ret = mfrc522_read_uid(dev, uid, &len);
		if (ret == 0) {
			misses = 0;
			uid_to_str(uid, len, uid_str);
			if (strcmp(uid_str, current) != 0) {
				strcpy(current, uid_str);
				ev.type = WW_EV_TAG_PLACED;
				strcpy(ev.tag.uid, current);
				ww_event_post(&ev);
			}
		} else {
			if (ret != -ENOENT) {
				LOG_DBG("Reading the tag failed: %d", ret);
			}
			if (current[0] != '\0' && ++misses >= CONFIG_WW_RFID_REMOVE_POLLS) {
				current[0] = '\0';
				ev.type = WW_EV_TAG_REMOVED;
				ww_event_post(&ev);
			}
		}

		k_msleep(CONFIG_WW_RFID_POLL_MS);
	}
}

K_THREAD_DEFINE(rfid_reader, 2048, reader_thread, NULL, NULL, NULL, 7, 0, 0);
