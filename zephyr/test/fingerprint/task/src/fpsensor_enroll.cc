/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor_test_utils.h"
#include "mock_fingerprint_algorithm.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>
#include <ec_commands.h>
#include <ec_tasks.h>
#include <fingerprint/fingerprint_alg.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

namespace
{

std::array<uint8_t, IMAGE_SIZE> image_buffer;

FpSensorInfoBuffer test_info_buffer;

int enroll_percent;
int enroll_step_return_val;
int custom_enroll_step(const fingerprint_algorithm *const alg,
		       const uint8_t *const image, int *percent)
{
	zassert_true(std::ranges::equal(std::span(image, image_buffer.size()),
					image_buffer));
	*percent = enroll_percent;

	return enroll_step_return_val;
}

void start_enroll_session()
{
	zassert_true(
		set_fp_mode(FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE) &
		(FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));
}

void simulate_finger_touch(bool load_test_image = true)
{
	update_sim_state([](auto &s) {
		s.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	});

	if (load_test_image) {
		image_buffer.fill(1);
		fingerprint_load_image(fp_sim, image_buffer.data(),
				       image_buffer.size());
	}

	fingerprint_run_callback(fp_sim);
	k_msleep(1);
}

void *fpsensor_setup()
{
	/* Start shimmed tasks. */
	start_ec_tasks();
	k_msleep(100);

	return nullptr;
}

void fpsensor_before(void *f)
{
	/* Reset global context to a clean state. */
	global_context.reset_and_clear_context();

	zassert_equal(set_fp_mode(0), 0);

	enroll_percent = 0;
	enroll_step_return_val = 0;

	reset_sim_state();
	RESET_FAKE(mkbp_send_event);
	reset_mock_alg_fakes();

	/* Clear MKBP events from previous tests. */
	(void)global_context.take_events();
}

} /* namespace */

ZTEST_USER(fpsensor_enroll, test_enroll_start_stop)
{
	/* Start enroll session. */
	start_enroll_session();

	/* Make sure the 'enroll_start' callback was called. */
	zassert_equal(mock_alg_enroll_start_fake.call_count, 1);

	/* Stop enroll session. */
	zassert_false(set_fp_mode(0) &
		      (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Confirm that enroll session is not running. */
	zassert_false(get_fp_mode() & FP_MODE_ENROLL_SESSION);

	/* Make sure the 'enroll_finish' callback was called. */
	zassert_equal(mock_alg_enroll_finish_fake.call_count, 1);
}

ZTEST_USER(fpsensor_enroll, test_enroll_start_failure)
{
	/* Return failure on attempt to start enroll session. */
	mock_alg_enroll_start_fake.return_val = -EINVAL;

	/* Try to start enroll session. */
	start_enroll_session();

	/* Confirm that enroll session is not running. */
	zassert_false(get_fp_mode() & FP_MODE_ENROLL_SESSION);

	/* Make sure that 'enroll_finish' callback was NOT called. */
	zassert_equal(mock_alg_enroll_finish_fake.call_count, 0);
}

ZTEST_USER(fpsensor_enroll, test_enroll_configure_detect)
{
	fingerprint_sensor_state state;

	/* Start enroll session. */
	start_enroll_session();

	/* Confirm that detect mode was enabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_true(state.detect_mode);

	/* Stop enroll session. */
	zassert_false(set_fp_mode(0) &
		      (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Confirm that detect mode was disabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_false(state.detect_mode);

	/* Confirm that the enrollment session was properly torn down. */
	zassert_equal(
		mock_alg_enroll_finish_fake.call_count, 1,
		"Algorithm finish should be called when explicitly closing the session");
}

ZTEST_USER(fpsensor_enroll, test_enroll_step)
{
	start_enroll_session();

	/*
	 * Use custom enroll step function to confirm that provided image is
	 * correct and return enroll progress to fpsensor task.
	 */
	enroll_percent = 33;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	simulate_finger_touch();

	/*
	 * Confirm that:
	 * - MKBP events are FINGER_DOWN and FP_ENROLL
	 * - No errors
	 * - Reported enroll progress is correct
	 */
	const FpMkbpEvent fp_events =
		take_mkbp_fp_events(mkbp_send_event_fake, 2);
	zassert_true(fp_events.is_finger_down());
	zassert_true(fp_events.is_enroll());
	zassert_equal(fp_events.error_code(), 0);
	zassert_equal(fp_events.enroll_progress(), enroll_percent);

	/*
	 * Confirm that a normal mid-enrollment step does not finish the
	 * session.
	 */
	zassert_equal(
		mock_alg_enroll_finish_fake.call_count, 0,
		"Algorithm finish should not be called during an active enrollment step");
}

ZTEST_USER(fpsensor_enroll, test_enroll_step_failure)
{
	start_enroll_session();

	/* Return critical error from enroll_step. */
	mock_alg_enroll_step_fake.return_val = -EINVAL;

	simulate_finger_touch(/*load_test_image=*/false);

	/*
	 * Confirm that:
	 * - MKBP events are FINGER_DOWN and FP_ENROLL
	 * - Internal error is reported
	 * - No progress is reported
	 */
	const FpMkbpEvent fp_events =
		take_mkbp_fp_events(mkbp_send_event_fake, 2);
	zassert_true(fp_events.is_finger_down());
	zassert_true(fp_events.is_enroll());
	zassert_equal(fp_events.error_code(), EC_MKBP_FP_ERR_ENROLL_INTERNAL);
	zassert_equal(fp_events.enroll_progress(), 0);

	/* Confirm that enroll session is still running. */
	zassert_true(get_fp_mode() & FP_MODE_ENROLL_SESSION);

	/*
	 * Confirm that the underlying enrollment session wasn't prematurely
	 * finished.
	 */
	zassert_equal(
		mock_alg_enroll_finish_fake.call_count, 0,
		"Algorithm finish should not be called on transient step failure");
}

ZTEST_USER(fpsensor_enroll, test_enroll_step_low_quality_warning)
{
	start_enroll_session();

	/*
	 * Use custom enroll step function to warn about low quality of the
	 * image and provide enroll progress to fpsensor task.
	 */
	enroll_percent = 33;
	enroll_step_return_val = FP_ENROLLMENT_RESULT_LOW_QUALITY;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	simulate_finger_touch();

	/*
	 * Confirm that:
	 * - MKBP events are FINGER_DOWN and FP_ENROLL
	 * - Low Quality warning is reported
	 * - Correct progress is reported
	 */
	const FpMkbpEvent fp_events =
		take_mkbp_fp_events(mkbp_send_event_fake, 2);
	zassert_true(fp_events.is_finger_down());
	zassert_true(fp_events.is_enroll());
	zassert_equal(fp_events.error_code(),
		      EC_MKBP_FP_ERR_ENROLL_LOW_QUALITY);
	zassert_equal(fp_events.enroll_progress(), enroll_percent);

	/* Confirm that enroll session is still running. */
	zassert_true(get_fp_mode() & FP_MODE_ENROLL_SESSION);

	/*
	 * Confirm that a low quality warning does not prematurely finish the
	 * session.
	 */
	zassert_equal(
		mock_alg_enroll_finish_fake.call_count, 0,
		"Algorithm finish should not be called on a transient quality warning");
}

ZTEST_USER(fpsensor_enroll, test_enroll_step_finish_failed)
{
	start_enroll_session();

	/*
	 * Use custom enroll step function to confirm that provided image is
	 * correct and return enroll progress to fpsensor task.
	 */
	enroll_percent = 100;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	/* Return error on call to enroll_finish. */
	mock_alg_enroll_finish_fake.return_val = -EINVAL;

	simulate_finger_touch();

	/*
	 * Confirm that:
	 * - MKBP events are FINGER_DOWN and FP_ENROLL
	 * - Internal error was returned
	 * - Reported enroll progress is correct
	 */
	const FpMkbpEvent fp_events =
		take_mkbp_fp_events(mkbp_send_event_fake, 2);
	zassert_true(fp_events.is_finger_down());
	zassert_true(fp_events.is_enroll());
	zassert_equal(fp_events.error_code(), EC_MKBP_FP_ERR_ENROLL_INTERNAL);
	zassert_equal(fp_events.enroll_progress(), enroll_percent);

	/* Confirm that enroll session is not running. */
	zassert_false(get_fp_mode() & FP_MODE_ENROLL_SESSION);

	/*
	 * Confirm that 'enroll_finish' was called after the conversion failure.
	 */
	zassert_equal(
		mock_alg_enroll_finish_fake.call_count, 1,
		"Algorithm finish should be called to close out the session");
}

ZTEST_USER(fpsensor_enroll, test_enroll_step_finish_success)
{
	start_enroll_session();

	/*
	 * Use custom enroll step function to confirm that provided image is
	 * correct and return enroll progress to fpsensor task.
	 */
	enroll_percent = 100;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	simulate_finger_touch();

	/* Confirm that 'enroll_finish' was called. */
	zassert_equal(mock_alg_enroll_finish_fake.call_count, 1);

	/*
	 * Confirm that:
	 * - MKBP event is FP_ENROLL
	 * - No errors
	 * - Reported enroll progress is correct
	 */
	const FpMkbpEvent fp_events =
		take_mkbp_fp_events(mkbp_send_event_fake, 2);
	zassert_true(fp_events.is_finger_down());
	zassert_true(fp_events.is_enroll());
	zassert_equal(fp_events.error_code(), 0);
	zassert_equal(fp_events.enroll_progress(), enroll_percent);

	/* Confirm that enroll session is not running. */
	zassert_false(get_fp_mode() & FP_MODE_ENROLL_SESSION);

	/* Confirm that there is 1 valid template. */
	zassert_ok(get_fp_info_v3(test_info_buffer));
	zassert_equal(test_info_buffer.header.template_info.template_valid, 1);
	/* Don't forget that template_dirty is a bitmask. */
	zassert_equal(test_info_buffer.header.template_info.template_dirty,
		      0x1);
}

ZTEST_USER(fpsensor_enroll, test_enroll_max_capacity_reached)
{
	start_enroll_session();

	/* Artificially simulate reaching full capacity mid-session. */
	global_context.templates.templ_valid = FP_MAX_FINGER_COUNT;

	simulate_finger_touch();

	/*
	 * Confirm that:
	 * - MKBP events are FINGER_DOWN and FP_ENROLL
	 * - Internal error was returned
	 */
	const FpMkbpEvent fp_events =
		take_mkbp_fp_events(mkbp_send_event_fake, 2);
	zassert_true(fp_events.is_finger_down());
	zassert_true(fp_events.is_enroll());
	zassert_equal(fp_events.error_code(), EC_MKBP_FP_ERR_ENROLL_INTERNAL);

	/*
	 * Confirm that the enrollment session was automatically torn down and
	 * the algorithm's state is properly cleaned up.
	 */
	zassert_false(get_fp_mode() & FP_MODE_ENROLL_SESSION,
		      "Enrollment session should have been deactivated");
	zassert_equal(mock_alg_enroll_finish_fake.call_count, 1,
		      "Algorithm enrollment state should be aborted");
}

ZTEST_USER(fpsensor_enroll, test_enroll_warning_when_previous_template_unread)
{
	/* Simulate a previously enrolled template that has not been read yet.
	 */
	global_context.templates.template_newly_enrolled =
		*FpTemplateId::create(0);

	start_enroll_session();

	enroll_percent = 50;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	simulate_finger_touch();

	const FpMkbpEvent fp_events =
		take_mkbp_fp_events(mkbp_send_event_fake, 2);
	zassert_true(fp_events.is_enroll());
	zassert_equal(fp_events.error_code(), 0);
	zassert_equal(fp_events.enroll_progress(), enroll_percent);
}

ZTEST_USER(fpsensor_enroll,
	   test_enroll_step_finish_with_fingerprint_auth_enabled)
{
	start_enroll_session();

	/* Enable Fingerprint Auth session before enrollment finishes. */
	global_context.crypto.set_auth_enabled(true);

	enroll_percent = 100;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	simulate_finger_touch();

	const FpMkbpEvent fp_events =
		take_mkbp_fp_events(mkbp_send_event_fake, 2);
	zassert_true(fp_events.is_enroll());
	zassert_equal(fp_events.error_code(), 0);
	zassert_equal(fp_events.enroll_progress(), 100);
	zassert_true(global_context.templates.template_newly_enrolled ==
		     FpTemplateId::create(0));
	/*
	 * When auth is enabled, templ_valid is not incremented until
	 * EC_CMD_FP_CONFIRM_TEMPLATE is called.
	 */
	zassert_equal(global_context.templates.templ_valid, 0);

	global_context.crypto.set_auth_enabled(false);
}

ZTEST_SUITE(fpsensor_enroll, nullptr, fpsensor_setup, fpsensor_before, nullptr,
	    nullptr);
