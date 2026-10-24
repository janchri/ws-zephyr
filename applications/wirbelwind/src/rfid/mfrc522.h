/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* NXP MFRC522 RFID reader driver (devicetree: "nxp,mfrc522" on a SPI bus) */

#ifndef WW_MFRC522_H_
#define WW_MFRC522_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

/* ISO 14443A UIDs are 4, 7 or 10 bytes */
#define MFRC522_UID_MAX 10

/*
 * Wake up a card in the field (also one that was halted before), read its
 * UID and halt it again, so polling with this detects whether a card is
 * still there.
 *
 * @retval 0       UID read, *len is 4, 7 or 10
 * @retval -ENOENT no card
 * @retval -EBUSY  more than one card in the field
 * @retval <0      communication error
 */
int mfrc522_read_uid(const struct device *dev, uint8_t uid[MFRC522_UID_MAX], size_t *len);

#endif /* WW_MFRC522_H_ */
