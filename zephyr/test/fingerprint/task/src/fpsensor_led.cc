/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
extern "C" {
#include "pwm_mock.h"
}

#include <zephyr/drivers/pwm.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/policy.h>
#include <zephyr/ztest.h>

#include <fpsensor/fpsensor_led.h>
#include <mkbp_event.h>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

/* LCOV_EXCL_START */
extern "C" void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(substate_id);
	ARG_UNUSED(state);
}

extern "C" void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);
	irq_unlock(0);
}
/* LCOV_EXCL_STOP */

const struct device *pwm_fp_led =
	DEVICE_DT_GET(DT_PWMS_CTLR(DT_ALIAS(pwm_fp_led)));

static bool is_suspend_locked()
{
	return pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
					      PM_ALL_SUBSTATES);
}

static void test_fp_led_init(void *data)
{
	fp_led::update_mode(0);
	zassert_false(is_suspend_locked(),
		      "Suspend should not be locked initially");
}

static int fp_led_get()
{
	return pwm_mock_get_duty(pwm_fp_led, 0);
}

constexpr uint32_t BRIGHTNESS_ENROLL = 34;
constexpr uint32_t BRIGHTNESS_MATCH = 34;
constexpr uint32_t BRIGHTNESS_MATCH_OK = 100;
constexpr uint32_t BRIGHTNESS_MATCH_NOK = 6;
constexpr uint32_t BRIGHTNESS_OFF = 0;

ZTEST(fp_led, test_enroll)
{
	fp_led::update_mode(FP_MODE_ENROLL_SESSION);
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), BRIGHTNESS_ENROLL, "Led %d%% after enroll",
		      BRIGHTNESS_ENROLL);
	zassert_true(is_suspend_locked(),
		     "Suspend should be locked during enroll");
	k_sleep(K_SECONDS(30));
	zassert_equal(fp_led_get(), BRIGHTNESS_ENROLL,
		      "Led %d%% 30s after enroll", BRIGHTNESS_ENROLL);
	zassert_true(is_suspend_locked(),
		     "Suspend should be locked during enroll");
	fp_led::update_mode(0);
	zassert_false(is_suspend_locked(),
		      "Suspend should be unlocked after off");
}

ZTEST(fp_led, test_match)
{
	fp_led::update_mode(FP_MODE_MATCH);
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), BRIGHTNESS_MATCH, "Led %d%% after match",
		      BRIGHTNESS_MATCH);
	zassert_true(is_suspend_locked(),
		     "Suspend should be locked during match");
	k_sleep(K_SECONDS(10));
	zassert_equal(fp_led_get(), BRIGHTNESS_OFF, "Led off 10s after match");
	zassert_false(is_suspend_locked(),
		      "Suspend should be unlocked after match timeout");
}

ZTEST(fp_led, test_match_ok)
{
	fp_led::update_match(true);
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), BRIGHTNESS_MATCH_OK,
		      "Led %d%% 0.5s after match OK", BRIGHTNESS_MATCH_OK);
	zassert_true(is_suspend_locked(),
		     "Suspend should be locked during match OK");
	k_sleep(K_SECONDS(2));
	zassert_equal(fp_led_get(), BRIGHTNESS_OFF,
		      "Led off 2s after match OK");
	zassert_false(is_suspend_locked(),
		      "Suspend should be unlocked after match OK timeout");
}

ZTEST(fp_led, test_match_nok)
{
	fp_led::update_match(false);
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), BRIGHTNESS_MATCH_NOK,
		      "Led %d%% after match NOK", BRIGHTNESS_MATCH_NOK);
	zassert_true(is_suspend_locked(),
		     "Suspend should be locked during match NOK");
	k_sleep(K_SECONDS(1));
	zassert_equal(fp_led_get(), BRIGHTNESS_OFF,
		      "Led off 1s after match NOK");
	zassert_false(is_suspend_locked(),
		      "Suspend should be unlocked after match NOK timeout");
}

ZTEST(fp_led, test_off)
{
	fp_led::update_mode(FP_MODE_ENROLL_SESSION);
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), BRIGHTNESS_ENROLL, "Led %d%% after enroll",
		      BRIGHTNESS_ENROLL);
	zassert_true(is_suspend_locked(),
		     "Suspend should be locked during enroll");
	fp_led::update_mode(0);
	zassert_equal(fp_led_get(), BRIGHTNESS_OFF, "Led off immediately");
	zassert_false(is_suspend_locked(),
		      "Suspend should be unlocked after off");
}

ZTEST(fp_led, test_off_do_not_disturb)
{
	fp_led::update_match(true);
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), BRIGHTNESS_MATCH_OK,
		      "Led %d%% after match OK", BRIGHTNESS_MATCH_OK);
	zassert_true(is_suspend_locked(),
		     "Suspend should be locked during match OK");
	fp_led::update_mode(0);
	zassert_equal(fp_led_get(), BRIGHTNESS_MATCH_OK, "Led still %d%%",
		      BRIGHTNESS_MATCH_OK);
	zassert_true(is_suspend_locked(),
		     "Suspend should remain locked while match OK is ongoing");
	k_sleep(K_SECONDS(2));
	zassert_equal(fp_led_get(), BRIGHTNESS_OFF,
		      "Led off 2s after match OK");
	zassert_false(is_suspend_locked(),
		      "Suspend should be unlocked after match OK timeout");
}

ZTEST_SUITE(fp_led, NULL, NULL, test_fp_led_init, NULL, NULL);
