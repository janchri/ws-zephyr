/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* X-Powers AC101 codec: DAC path to headphone and speaker, I2S slave, 44.1 kHz */

#ifndef WW_AC101_H_
#define WW_AC101_H_

#include <stdbool.h>
#include <stdint.h>

/* Reset and configure the codec; outputs stay muted until ac101_enable_output() */
int ac101_init(void);

/* Analog output levels: headphone 0..63, speaker 0..31 */
int ac101_set_output_volume(uint8_t headphone, uint8_t speaker);

/* Outputs and speaker amplifier on/off */
int ac101_enable_output(bool enable);

#endif /* WW_AC101_H_ */
