/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "calendar_model.h"

#include <stdbool.h>

static int days_in_month(int year, int month)
{
	static const unsigned char month_lengths[] = {31, 28, 31, 30, 31, 30,
						      31, 31, 30, 31, 30, 31};
	const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;

	return month_lengths[month] + (month == 1 && leap);
}

static int first_weekday_monday(int year, int month)
{
	/* Zeller's congruence, converted to Monday-first numbering. */
	int adjusted_year = year;
	int adjusted_month = month + 1;

	if (adjusted_month < 3) {
		adjusted_month += 12;
		adjusted_year--;
	}
	int weekday = (1 + 13 * (adjusted_month + 1) / 5 + adjusted_year + adjusted_year / 4 -
		       adjusted_year / 100 + adjusted_year / 400) %
		      7;

	return (weekday + 5) % 7;
}

void desk_hub_calendar_model_build(const struct tm *now, int month_offset,
				   struct desk_hub_calendar_data *data)
{
	int year = now->tm_year + 1900;
	int month = now->tm_mon + month_offset;

	while (month < 0) {
		month += 12;
		year--;
	}
	while (month >= 12) {
		month -= 12;
		year++;
	}

	data->current_day = now->tm_mday;
	data->current_weekday = (now->tm_wday + 6) % 7;
	data->hour = now->tm_hour;
	data->minute = now->tm_min;
	data->second = now->tm_sec;
	data->year = year;
	data->month = month;
	data->first_weekday = first_weekday_monday(year, month);
	data->days_in_month = days_in_month(year, month);
	data->today = month_offset == 0 ? now->tm_mday : -1;
}
