/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_CONWAY_MODEL_H_
#define DESK_HUB_CONWAY_MODEL_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/** Maximum number of rows or columns in the singleton Life model. */
#define CONWAY_MAX_CELLS 100

/** @brief Fill length bytes with random data for the random seed. */
typedef void (*conway_random_fn)(uint8_t *buffer, size_t length);

enum conway_seed {
	CONWAY_SEED_ACHIMS_P144,
	CONWAY_SEED_BREEDER,
	CONWAY_SEED_ACHIMS_OTHER_P16,
	CONWAY_SEED_RANDOM,
	CONWAY_SEED_COUNT,
};

/**
 * @brief Initialize the singleton Life model on the controller thread.
 * @param cols Grid width (74..CONWAY_MAX_CELLS; two guns require 74 columns).
 * @param rows Grid height (36..CONWAY_MAX_CELLS).
 * @param seed Initial seed, less than CONWAY_SEED_COUNT.
 * @param random Random byte source, retained until the next initialization.
 * @return 0 on success, -EINVAL for invalid dimensions, seed or callback.
 */
int conway_model_init(int cols, int rows, enum conway_seed seed, conway_random_fn random);
/** @brief Replace the initialized grid. Seed must be less than CONWAY_SEED_COUNT. */
void conway_model_set_seed(enum conway_seed seed);
/** @brief Advance the grid by one Life generation. */
void conway_model_step(void);
/** @brief Return whether a cell is alive; coordinates outside the grid return false. */
bool conway_model_cell(int x, int y);
/** @brief Borrow a static display name; invalid seeds return "Unknown". */
const char *conway_model_seed_name(enum conway_seed seed);

#endif /* DESK_HUB_CONWAY_MODEL_H_ */
