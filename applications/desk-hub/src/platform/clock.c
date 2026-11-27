/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "clock.h"
#include <time.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/printk.h>

static int build_month_number(const char *month)
{
	static const char *const names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
					    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	for (int i = 0; i < ARRAY_SIZE(names); i++) {
		if (month[0] == names[i][0] && month[1] == names[i][1] && month[2] == names[i][2]) {
			return i + 1;
		}
	}
	return 1;
}

static int64_t build_time_epoch(void)
{
	const char date[] = __DATE__;
	const char time[] = __TIME__;
	const int month = build_month_number(date);
	const int day = (date[4] == ' ' ? 0 : date[4] - '0') * 10 + date[5] - '0';
	const int year = (date[7] - '0') * 1000 + (date[8] - '0') * 100 + (date[9] - '0') * 10 +
			 date[10] - '0';
	const int hour = (time[0] - '0') * 10 + time[1] - '0';
	const int minute = (time[3] - '0') * 10 + time[4] - '0';
	const int second = (time[6] - '0') * 10 + time[7] - '0';
	const int adjusted_year = year - (month <= 2);
	const int era = (adjusted_year >= 0 ? adjusted_year : adjusted_year - 399) / 400;
	const unsigned year_of_era = adjusted_year - era * 400;
	const unsigned adjusted_month = month + (month > 2 ? -3 : 9);
	const unsigned day_of_year = (153 * adjusted_month + 2) / 5 + day - 1;
	const unsigned day_of_era =
		year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
	const int64_t days = (int64_t)era * 146097 + day_of_era - 719468;

	return days * 86400 + hour * 3600 + minute * 60 + second;
}

void desk_hub_clock_init(void)
{
	struct timespec now;

	if (sys_clock_gettime(SYS_CLOCK_REALTIME, &now) == 0 && now.tv_sec >= 1577836800) {
		return;
	}

	now.tv_sec = build_time_epoch();
	now.tv_nsec = 0;
	(void)sys_clock_settime(SYS_CLOCK_REALTIME, &now);
	printk("Clock initialized from firmware build time\n");
}
