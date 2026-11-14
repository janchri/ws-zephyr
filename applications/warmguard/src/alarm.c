/* SPDX-License-Identifier: Apache-2.0 */
#include "alarm.h"

/**
 * @brief Decide whether a temperature alarm is required.
 * @satisfies DEMO-SW-001
 * @satisfies DEMO-SW-002
 */
bool alarm_required(int temperature_c)
{
	return temperature_c >= 50;
}
