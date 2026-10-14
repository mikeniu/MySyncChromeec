// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system.h"
#include "timer.h"

#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

static const int rtc_delay_seconds = 1;
static atomic_t interrupt_counter;
static atomic_t rtc_fired;

void rtc_callback(const struct device *dev)
{
	ARG_UNUSED(dev);
	atomic_add(&interrupt_counter, 1);
}

ZTEST_SUITE(rtc_npcx9, NULL, NULL, NULL, NULL, NULL);

ZTEST(rtc_npcx9, test_rtc_alarm_fired)
{
	atomic_clear(&interrupt_counter);
	system_set_rtc_alarm(rtc_delay_seconds, 0);

	k_sleep(K_SECONDS(2 * rtc_delay_seconds));

	rtc_fired = atomic_get(&interrupt_counter);

	zassert_equal(1, rtc_fired);
	zassert_equal(0, system_get_rtc_alarm());
}

ZTEST(rtc_npcx9, test_rtc_alarm_not_fired)
{
	atomic_clear(&interrupt_counter);
	system_set_rtc_alarm(rtc_delay_seconds, 0);

	k_sleep(K_SECONDS(0.5 * rtc_delay_seconds));

	rtc_fired = atomic_get(&interrupt_counter);

	zassert_equal(0, rtc_fired);
	/*
	 * In Zephyr, the `system_set_rtc_alarm` function (defined in
	 * src/platform/ec/zephyr/shim/src/rtc.c) adds an extra second to the
	 * alarm time (seconds += system_get_rtc_sec() + 1). This is to account
	 * for truncation of `system_get_rtc_sec()` and prevent missed alarms.
	 * The original EC code does not perform this addition. Therefore, we
	 * assert for '2' here, whereas in EC, the expectation is '1'.
	 */
	zassert_equal(2, system_get_rtc_alarm());
}

static const int rtc_alarm_iterations = 3;

ZTEST(rtc_npcx9, test_rtc_series_alarm_fired)
{
	atomic_clear(&interrupt_counter);

	for (int i = 0; i < rtc_alarm_iterations; ++i) {
		system_set_rtc_alarm(rtc_delay_seconds, 0);
		k_sleep(K_SECONDS(2 * rtc_delay_seconds));
		rtc_fired = atomic_get(&interrupt_counter);
		zassert_equal(i + 1, rtc_fired);
		zassert_equal(0, system_get_rtc_alarm());
	}
}
