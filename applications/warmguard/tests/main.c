/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include "alarm.h"

/** @defgroup alarm_module Temperature alarm verification */
/** @defgroup alarm Alarm Ztest suite
 * @ingroup alarm_module
 * @{
 */

/**
 * @brief Check below-threshold, boundary, and above-threshold temperatures.
 * @testid{DEMO-TC-001}
 * @verifies DEMO-SW-001
 * @verifies DEMO-SW-002
 */
ZTEST(alarm, test_threshold)
{
	zassert_false(alarm_required(49), "49 C must not trigger the alarm");
	zassert_true(alarm_required(50), "50 C must trigger the alarm");
	zassert_true(alarm_required(51), "51 C must trigger the alarm");
}

ZTEST_SUITE(alarm, NULL, NULL, NULL, NULL, NULL);

/** @} */
