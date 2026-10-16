/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor_test_utils.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <chrono>
#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>
#include <ec_commands.h>
#include <ec_tasks.h>
#include <fpsensor/fpsensor_state.h>
#include <fpsensor/fpsensor_state_machine.h>
#include <host_command.h>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

/*
 * Time we wait for fpsensor task to check if the finger was removed.
 * Derived directly from FpSensorStateMachine::kFingerPollingDelay.
 */
constexpr auto kPollingDelayMs =
	std::chrono::duration_cast<std::chrono::milliseconds>(
		FpSensorStateMachine::kFingerPollingDelay)
		.count();

static void set_finger_state(enum fingerprint_finger_state finger_state,
			     bool trigger_irq = false)
{
	update_sim_state([finger_state](auto &state) {
		state.finger_state = finger_state;
	});

	if (trigger_irq) {
		fingerprint_run_callback(fp_sim);
		k_msleep(1);
	}
}

ZTEST_USER(fpsensor_finger_presence, test_finger_down_mode)
{
	struct fingerprint_sensor_state state;

	/* Detect finger on the sensor. */
	zassert_true(set_fp_mode(FP_MODE_FINGER_DOWN) & FP_MODE_FINGER_DOWN);

	/* Confirm that fpsensor task is waiting for finger. */
	zassert_true(get_fp_mode() & FP_MODE_FINGER_DOWN);

	/* Confirm that detect mode was enabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_true(state.detect_mode);

	/* Disable finger detection */
	zassert_false(set_fp_mode(0) & FP_MODE_FINGER_DOWN);

	/* Confirm that detect mode was disabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_false(state.detect_mode);
}

ZTEST_USER(fpsensor_finger_presence, test_finger_down_present)
{
	uint32_t fp_events;

	/* Detect finger on the sensor. */
	zassert_true(set_fp_mode(FP_MODE_FINGER_DOWN) & FP_MODE_FINGER_DOWN);

	/* Put finger on the sensor and ping fpsensor task. */
	set_finger_state(FINGERPRINT_FINGER_STATE_PRESENT,
			 /*trigger_irq=*/true);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);
	fp_get_next_event(reinterpret_cast<uint8_t *>(&fp_events));
	zassert_true(FpMkbpEvent{ fp_events }.is_finger_down());

	/*
	 * Confirm that finger down flag is not set after the finger is
	 * detected.
	 */
	zassert_false(get_fp_mode() & FP_MODE_FINGER_DOWN);
}

ZTEST_USER(fpsensor_finger_presence, test_finger_down_partial)
{
	/* Detect finger on the sensor. */
	zassert_true(set_fp_mode(FP_MODE_FINGER_DOWN) & FP_MODE_FINGER_DOWN);

	/* Put finger partially on the sensor and ping fpsensor task. */
	set_finger_state(FINGERPRINT_FINGER_STATE_PARTIAL,
			 /*trigger_irq=*/true);

	/* Confirm MKBP event was not sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 0);

	/* Confirm that finger down flag is still set. */
	zassert_true(get_fp_mode() & FP_MODE_FINGER_DOWN);
}

ZTEST_USER(fpsensor_finger_presence, test_finger_down_no_finger)
{
	/* Detect finger on the sensor. */
	zassert_true(set_fp_mode(FP_MODE_FINGER_DOWN) & FP_MODE_FINGER_DOWN);

	/* Ping fpsensor task, but don't put finger on the sensor. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm MKBP event was not sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 0);

	/* Confirm that finger down flag is still set. */
	zassert_true(get_fp_mode() & FP_MODE_FINGER_DOWN);
}

ZTEST_USER(fpsensor_finger_presence, test_finger_up_mode)
{
	struct fingerprint_sensor_state state;

	/* Put finger on the sensor. */
	set_finger_state(FINGERPRINT_FINGER_STATE_PRESENT);

	/* Detect finger up. */
	zassert_true(set_fp_mode(FP_MODE_FINGER_UP) & FP_MODE_FINGER_UP);

	/* Confirm that fpsensor task is waiting for finger up. */
	zassert_true(get_fp_mode() & FP_MODE_FINGER_UP);

	/* Confirm that detect mode was enabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_true(state.detect_mode);

	/* Disable finger up detection */
	zassert_false(set_fp_mode(0) & FP_MODE_FINGER_UP);

	/* Confirm that detect mode was disabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_false(state.detect_mode);
}

ZTEST_USER(fpsensor_finger_presence, test_finger_up_no_finger_no_interrupt)
{
	/* Detect finger up. */
	zassert_true(set_fp_mode(FP_MODE_FINGER_UP) & FP_MODE_FINGER_UP);

	/* Confirm that fpsensor task is waiting for finger up. */
	zassert_true(get_fp_mode() & FP_MODE_FINGER_UP);

	/* Check that no MKBP event was triggered yet. */
	zassert_equal(mkbp_send_event_fake.call_count, 0);

	/* Wait for fpsensor task to check the finger state. */
	k_msleep(kPollingDelayMs);

	/* Confirm MKBP event was sent. */
	zassert_true(take_mkbp_fp_events(mkbp_send_event_fake).is_finger_up());

	/* Confirm that finger up flag is not set. */
	zassert_false(get_fp_mode() & FP_MODE_FINGER_UP);
}

ZTEST_USER(fpsensor_finger_presence,
	   test_finger_up_present_then_no_finger_interrupt)
{
	/* Put finger on the sensor. */
	set_finger_state(FINGERPRINT_FINGER_STATE_PRESENT);

	/* Detect finger up. */
	zassert_true(set_fp_mode(FP_MODE_FINGER_UP) & FP_MODE_FINGER_UP);

	/* Remove finger from the sensor and ping fpsensor task. */
	set_finger_state(FINGERPRINT_FINGER_STATE_NONE,
			 /*trigger_irq=*/true);

	/* Confirm MKBP event was sent. */
	zassert_true(take_mkbp_fp_events(mkbp_send_event_fake).is_finger_up());

	/* Confirm that finger up flag is not set after the finger is up. */
	zassert_false(get_fp_mode() & FP_MODE_FINGER_UP);
}

ZTEST_USER(fpsensor_finger_presence,
	   test_finger_up_present_then_no_finger_no_interrupt)
{
	/* Put finger on the sensor. */
	set_finger_state(FINGERPRINT_FINGER_STATE_PRESENT);

	/* Detect finger up. */
	zassert_true(set_fp_mode(FP_MODE_FINGER_UP) & FP_MODE_FINGER_UP);

	/* Remove finger from the sensor. */
	set_finger_state(FINGERPRINT_FINGER_STATE_NONE);

	/* Wait for fpsensor task to check the finger state. */
	k_msleep(kPollingDelayMs);

	/* Confirm MKBP event was sent. */
	zassert_true(take_mkbp_fp_events(mkbp_send_event_fake).is_finger_up());

	/* Confirm that finger up flag is not set after the finger is up. */
	zassert_false(get_fp_mode() & FP_MODE_FINGER_UP);
}

ZTEST_USER(fpsensor_finger_presence,
	   test_finger_up_present_then_partial_no_interrupt)
{
	/* Put finger on the sensor. */
	set_finger_state(FINGERPRINT_FINGER_STATE_PRESENT);

	/* Detect finger up. */
	zassert_true(set_fp_mode(FP_MODE_FINGER_UP) & FP_MODE_FINGER_UP);

	/* Put finger partially on the sensor. */
	set_finger_state(FINGERPRINT_FINGER_STATE_PARTIAL);

	/* Wait for fpsensor task to check the finger state. */
	k_msleep(kPollingDelayMs);

	/* Confirm MKBP event was not sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 0);

	/* Confirm that finger up flag is still set. */
	zassert_true(get_fp_mode() & FP_MODE_FINGER_UP);
}

ZTEST_USER(fpsensor_finger_presence,
	   test_finger_up_partial_then_no_finger_no_interrupt)
{
	/* Put finger on the sensor. */
	set_finger_state(FINGERPRINT_FINGER_STATE_PARTIAL);

	/* Detect finger up. */
	zassert_true(set_fp_mode(FP_MODE_FINGER_UP) & FP_MODE_FINGER_UP);

	/* Remove finger from the sensor. */
	set_finger_state(FINGERPRINT_FINGER_STATE_NONE);

	/* Wait for fpsensor task to check the finger state. */
	k_msleep(kPollingDelayMs);

	/* Confirm MKBP event was sent. */
	zassert_true(take_mkbp_fp_events(mkbp_send_event_fake).is_finger_up());

	/* Confirm that finger up flag is not set after the finger is up. */
	zassert_false(get_fp_mode() & FP_MODE_FINGER_UP);
}

static void *fpsensor_setup(void)
{
	/* Start shimmed tasks. */
	start_ec_tasks();
	k_msleep(100);

	return nullptr;
}

static void fpsensor_before(void *f)
{
	zassert_equal(set_fp_mode(0), 0);

	reset_sim_state();
	RESET_FAKE(mkbp_send_event);
}

ZTEST_SUITE(fpsensor_finger_presence, nullptr, fpsensor_setup, fpsensor_before,
	    nullptr, nullptr);
