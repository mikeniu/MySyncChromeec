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
#include <zephyr/ztest.h>

#include <fpsensor/fpsensor_led.h>
#include <mkbp_event.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

const struct device *pwm_fp_led =
	DEVICE_DT_GET(DT_PWMS_CTLR(DT_ALIAS(pwm_fp_led)));

static void test_fp_led_init(void *data)
{
	fp_led::off();
}

static int fp_led_get()
{
	return pwm_mock_get_duty(pwm_fp_led, 0);
}

ZTEST(fp_led, test_enroll)
{
	fp_led::enroll();
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), 70, "Led 70% after enroll");
	k_sleep(K_SECONDS(30));
	zassert_equal(fp_led_get(), 0, "Led off 30s after enroll");
}

ZTEST(fp_led, test_match)
{
	fp_led::match();
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), 70, "Led 70% after match");
	k_sleep(K_SECONDS(30));
	zassert_equal(fp_led_get(), 0, "Led off 30s after match");
}

ZTEST(fp_led, test_match_ok)
{
	fp_led::match_ok();
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), 10, "Led 10% 0.5s after match OK");
	k_sleep(K_SECONDS(1));
	zassert_equal(fp_led_get(), 70, "Led 70% 1.5s after match OK");
	k_sleep(K_SECONDS(1));
	zassert_equal(fp_led_get(), 10, "Led 10% 2.5s after match OK");
	k_sleep(K_SECONDS(1));
	zassert_equal(fp_led_get(), 70, "Led 70% 3.5s after match OK");
	k_sleep(K_SECONDS(1));
	zassert_equal(fp_led_get(), 10, "Led 10% 4.5s after match OK");
	k_sleep(K_SECONDS(1));
	zassert_equal(fp_led_get(), 0, "Led off 5s after match OK");
}

ZTEST(fp_led, test_match_nok)
{
	fp_led::match_nok();
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), 10, "Led 70% after match NOK");
	k_sleep(K_SECONDS(2));
	zassert_equal(fp_led_get(), 0, "Led off 2s after match NOK");
}

ZTEST(fp_led, test_off)
{
	fp_led::enroll();
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), 70, "Led 70% after enroll");
	fp_led::off();
	zassert_equal(fp_led_get(), 0, "Led off immediately");
}

ZTEST(fp_led, test_off_do_not_disturb)
{
	fp_led::match_nok();
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), 10, "Led 10% after match NOK");
	fp_led::off();
	zassert_equal(fp_led_get(), 10, "Led still 10%");
	k_sleep(K_SECONDS(2));
	zassert_equal(fp_led_get(), 0, "Led off 2s after match NOK");
}

ZTEST_SUITE(fp_led, NULL, NULL, test_fp_led_init, NULL, NULL);
