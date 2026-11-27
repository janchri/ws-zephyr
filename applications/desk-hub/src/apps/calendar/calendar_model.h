/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_CALENDAR_MODEL_H_
#define DESK_HUB_CALENDAR_MODEL_H_

#include <time.h>

/** @brief Calendar presentation data, independent of the UI and system clock. */
struct desk_hub_calendar_data {
	int current_day;     /**< Actual day of month, independent of the browsed month. */
	int current_weekday; /**< Actual weekday, Monday = 0. */
	int hour;
	int minute;
	int second;
	int year;
	int month;         /**< January = 0, December = 11. */
	int first_weekday; /**< Monday = 0, Sunday = 6. */
	int days_in_month;
	int today; /**< Day to highlight, or -1 when viewing a different month. */
};

/**
 * @brief Build calendar data without changing the source time.
 * @param now Valid UTC calendar time; must not be NULL.
 * @param month_offset Signed offset from the current month.
 * @param data Destination for the selected month and current clock time.
 */
void desk_hub_calendar_model_build(const struct tm *now, int month_offset,
				   struct desk_hub_calendar_data *data);

#endif /* DESK_HUB_CALENDAR_MODEL_H_ */
