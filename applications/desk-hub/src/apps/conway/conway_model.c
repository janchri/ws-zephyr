/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "conway_model.h"

#include <string.h>

#include <errno.h>

#define MAX_CELLS CONWAY_MAX_CELLS
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static conway_random_fn random_bytes;

static uint8_t generations[2][MAX_CELLS][MAX_CELLS];
static int active;
static int cols;
static int rows;

static const char *const seed_names[] = {
	[CONWAY_SEED_ACHIMS_P144] = "Achim p144",
	[CONWAY_SEED_BREEDER] = "Breeder",
	[CONWAY_SEED_ACHIMS_OTHER_P16] = "Achim other p16",
	[CONWAY_SEED_RANDOM] = "Random",
};

static void seed_rle(uint8_t grid[MAX_CELLS][MAX_CELLS], int cols, int rows, const char *rle,
		     int pattern_width, int pattern_height)
{
	const int x_offset = (cols - pattern_width) / 2;
	const int y_offset = (rows - pattern_height) / 2;
	int x = 0;
	int y = 0;
	int run = 0;

	for (; *rle != '\0'; rle++) {
		const char token = *rle;

		if (token >= '0' && token <= '9') {
			run = run * 10 + token - '0';
			continue;
		}

		const int count = run == 0 ? 1 : run;
		run = 0;
		if (token == 'o') {
			for (int cell = 0; cell < count; cell++) {
				grid[y_offset + y][x_offset + x + cell] = 1;
			}
			x += count;
		} else if (token == 'b') {
			x += count;
		} else if (token == '$') {
			y += count;
			x = 0;
		} else if (token == '!') {
			break;
		}
	}
}

static void seed_breeder(uint8_t grid[MAX_CELLS][MAX_CELLS], int cols, int rows)
{
	/* Two centered Gosper guns fire diagonally away from one another. */
	static const uint8_t gun[][2] = {
		{24, 0}, {22, 1}, {24, 1}, {12, 2}, {13, 2}, {20, 2}, {21, 2}, {34, 2}, {35, 2},
		{11, 3}, {15, 3}, {20, 3}, {21, 3}, {34, 3}, {35, 3}, {0, 4},  {1, 4},  {10, 4},
		{16, 4}, {20, 4}, {21, 4}, {0, 5},  {1, 5},  {10, 5}, {14, 5}, {16, 5}, {17, 5},
		{22, 5}, {24, 5}, {10, 6}, {16, 6}, {24, 6}, {11, 7}, {15, 7}, {12, 8}, {13, 8},
	};
	const int y = (rows - 9) / 2;
	const int left_x = (cols - 74) / 2;
	const int right_x = left_x + 38;

	for (size_t i = 0; i < (sizeof(gun) / sizeof(gun[0])); i++) {
		const int x = gun[i][0];
		const int row = gun[i][1];

		/* The original gun fires down-right; its 180-degree rotation fires up-left. */
		grid[y + row][left_x + x] = 1;
		grid[y + 8 - row][right_x + 35 - x] = 1;
	}
}

static void seed_pattern(uint8_t grid[MAX_CELLS][MAX_CELLS], enum conway_seed preset, int cols,
			 int rows)
{
	static const char achims_p144_rle[] =
		"2o24b2o$2o24b2o$18b2o$17bo2bo$18b2o$14bo$13bobo$12bo3bo$12bo2bo2$"
		"12bo2bo$11bo3bo$12bobo$13bo$8b2o$7bo2bo$8b2o$2o24b2o$2o24b2o!";
	static const char achims_other_p16_rle[] =
		"9bo15bo$9b2o13b2o$9b2o13b2o$10bo13bo5$12b2o7b2o$"
		"3o10b2o5b2o10b3o$b3o10b2o3b2o10b3o2$8bo17bo$8b2o15b2o$"
		"9b2o13b2o$10bo13bo4$10bo13bo$9b2o13b2o$8b2o15b2o$8bo17bo2$"
		"b3o10b2o3b2o10b3o$3o10b2o5b2o10b3o$12b2o7b2o5$"
		"10bo13bo$9b2o13b2o$9b2o13b2o$9bo15bo!";

	memset(grid, 0, sizeof(generations[0]));

	switch (preset) {
	case CONWAY_SEED_ACHIMS_P144:
		/* 42-cell, period-144 oscillator: conwaylife.com/wiki/Achim's_p144. */
		seed_rle(grid, cols, rows, achims_p144_rle, 28, 19);
		break;
	case CONWAY_SEED_BREEDER:
		seed_breeder(grid, cols, rows);
		break;
	case CONWAY_SEED_ACHIMS_OTHER_P16:
		/* 96-cell, period-16 oscillator: conwaylife.com/wiki/Achim's_other_p16. */
		seed_rle(grid, cols, rows, achims_other_p16_rle, 35, 35);
		break;
	case CONWAY_SEED_RANDOM:
		for (int y = 0; y < rows; y++) {
			random_bytes(grid[y], cols);
			for (int x = 0; x < cols; x++) {
				grid[y][x] &= 1;
			}
		}
		break;
	default:
		break;
	}
}

static void step(const uint8_t grid[MAX_CELLS][MAX_CELLS], uint8_t next[MAX_CELLS][MAX_CELLS],
		 int cols, int rows)
{
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			int neighbors = -grid[y][x];

			for (int row = MAX(y - 1, 0); row <= MIN(y + 1, rows - 1); row++) {
				for (int col = MAX(x - 1, 0); col <= MIN(x + 1, cols - 1); col++) {
					neighbors += grid[row][col];
				}
			}

			next[y][x] = neighbors == 3 || (neighbors == 2 && grid[y][x]);
		}
	}
}

int conway_model_init(int width, int height, enum conway_seed seed, conway_random_fn random)
{
	if (width < 74 || width > MAX_CELLS || height < 36 || height > MAX_CELLS ||
	    (unsigned int)seed >= CONWAY_SEED_COUNT || random == NULL) {
		return -EINVAL;
	}
	random_bytes = random;
	cols = width;
	rows = height;
	active = 0;
	seed_pattern(generations[active], seed, cols, rows);
	return 0;
}

void conway_model_set_seed(enum conway_seed seed)
{
	active = 1 - active;
	seed_pattern(generations[active], seed, cols, rows);
}

void conway_model_step(void)
{
	const int next = 1 - active;
	step(generations[active], generations[next], cols, rows);
	active = next;
}

bool conway_model_cell(int x, int y)
{
	return x >= 0 && x < cols && y >= 0 && y < rows && generations[active][y][x];
}

const char *conway_model_seed_name(enum conway_seed seed)
{
	return (unsigned int)seed < CONWAY_SEED_COUNT ? seed_names[seed] : "Unknown";
}
