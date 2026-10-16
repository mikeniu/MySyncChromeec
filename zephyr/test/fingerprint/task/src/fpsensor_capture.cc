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

#include <array>
#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>
#include <ec_commands.h>
#include <ec_tasks.h>
#include <fpsensor/fpsensor_image_frame_params.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

static std::array<uint8_t, IMAGE_SIZE> image_buffer;
static std::array<uint8_t, IMAGE_SIZE> frame_buffer;

static void
start_capture_mode(enum fp_capture_type capture_type = FP_CAPTURE_SIMPLE_IMAGE)
{
	const uint32_t mode = set_fp_mode(
		FP_MODE_CAPTURE |
		FpCaptureType::create(capture_type)->to_mode_bits());
	zassert_true(mode & FP_MODE_CAPTURE);
	zassert_equal(FP_CAPTURE_TYPE(mode), capture_type);
}

static void simulate_finger_touch(enum fingerprint_finger_state finger_state =
					  FINGERPRINT_FINGER_STATE_PRESENT)
{
	update_sim_state([finger_state](auto &state) {
		state.finger_state = finger_state;
	});
	fingerprint_run_callback(fp_sim);
	k_msleep(1);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_simple_image_detection_enabled)
{
	struct fingerprint_sensor_state state;

	/* Switch mode to capture. */
	start_capture_mode();

	/* Confirm that detect mode was enabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_true(state.detect_mode);

	/* Disable finger capture. */
	zassert_false(set_fp_mode(0) & FP_MODE_CAPTURE);

	/* Confirm that detect mode was disabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_false(state.detect_mode);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_simple_image_mode_is_correct)
{
	struct fingerprint_sensor_state state;

	start_capture_mode();
	simulate_finger_touch();

	/* Confirm that correct mode was passed to driver. */
	fingerprint_get_state(fp_sim, &state);
	zassert_equal(state.last_acquire_image_mode,
		      FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_finger_state_partial)
{
	struct fingerprint_sensor_state state;

	start_capture_mode();
	simulate_finger_touch(FINGERPRINT_FINGER_STATE_PARTIAL);

	/* Confirm that no scan was performed. */
	fingerprint_get_state(fp_sim, &state);
	zassert_equal(state.last_acquire_image_mode, -1);

	/* Confirm that capture mode is still enabled. */
	zassert_true(get_fp_mode() & FP_MODE_CAPTURE);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_finger_state_none)
{
	struct fingerprint_sensor_state state;

	start_capture_mode();
	simulate_finger_touch(FINGERPRINT_FINGER_STATE_NONE);

	/* Confirm that no scan was performed. */
	fingerprint_get_state(fp_sim, &state);
	zassert_equal(state.last_acquire_image_mode, -1);

	/* Confirm that capture mode is still enabled. */
	zassert_true(get_fp_mode() & FP_MODE_CAPTURE);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_simple_image_scan_too_fast)
{
	start_capture_mode();

	update_sim_state([](auto &state) {
		state.acquire_image_result = FINGERPRINT_SENSOR_SCAN_TOO_FAST;
	});

	simulate_finger_touch();

	/* Confirm MKBP event was sent once (FINGER_DOWN). */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_history[0],
		      EC_MKBP_EVENT_FINGERPRINT);

	zassert_true(
		FpMkbpEvent{ global_context.take_events() }.is_finger_down());

	/* Confirm that capture mode is still enabled. */
	zassert_true(get_fp_mode() & FP_MODE_CAPTURE);

	/* Ping fpsensor task the second time. */
	fingerprint_run_callback(fp_sim);
	k_msleep(1);

	/* Confirm MKBP events were sent twice. */
	zassert_equal(mkbp_send_event_fake.call_count, 2);
	zassert_equal(mkbp_send_event_fake.arg0_history[0],
		      EC_MKBP_EVENT_FINGERPRINT);
	zassert_equal(mkbp_send_event_fake.arg0_history[1],
		      EC_MKBP_EVENT_FINGERPRINT);

	zassert_true(
		FpMkbpEvent{ global_context.take_events() }.is_finger_down());
}

ZTEST_USER(fpsensor_capture, test_finger_capture_simple_image_scan_good)
{
	start_capture_mode();
	simulate_finger_touch();

	/* Confirm MKBP events were sent twice. */
	zassert_equal(mkbp_send_event_fake.call_count, 2);
	zassert_equal(mkbp_send_event_fake.arg0_history[0],
		      EC_MKBP_EVENT_FINGERPRINT);
	zassert_equal(mkbp_send_event_fake.arg0_history[1],
		      EC_MKBP_EVENT_FINGERPRINT);

	const FpMkbpEvent fp_events{ global_context.take_events() };
	zassert_true(fp_events.is_finger_down());
	zassert_true(fp_events.is_image_ready());

	/* Ping fpsensor task the second time. */
	fingerprint_run_callback(fp_sim);
	k_msleep(1);

	/* Check that the FP_MODE_CAPTURE bit is turned off */
	const uint32_t mode = get_fp_mode();
	zassert_false(mode & FP_MODE_CAPTURE,
		      "Capture mode should be deactivated (got mode 0x%08x)",
		      mode);

	/* Assert no secondary FINGER_DOWN event was dispatched. */
	zassert_equal(
		mkbp_send_event_fake.call_count, 2,
		"No additional MKBP event should be sent after capture mode is deactivated");
}

ZTEST_USER(fpsensor_capture,
	   test_finger_capture_simple_image_scan_success_mode_cleared)
{
	start_capture_mode();
	simulate_finger_touch();

	/* Confirm that capture mode is not enabled. */
	zassert_false(get_fp_mode() & FP_MODE_CAPTURE);
}

ZTEST_USER(fpsensor_capture,
	   test_finger_capture_simple_image_scan_success_mkbp_event)
{
	start_capture_mode();
	simulate_finger_touch();

	/* Confirm MKBP events were sent twice. */
	zassert_equal(mkbp_send_event_fake.call_count, 2);
	zassert_equal(mkbp_send_event_fake.arg0_history[0],
		      EC_MKBP_EVENT_FINGERPRINT);
	zassert_equal(mkbp_send_event_fake.arg0_history[1],
		      EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that both EC_MKBP_FP_FINGER_DOWN and FP_IMAGE_READY MKBP
	 * events were sent.
	 */
	const FpMkbpEvent fp_events{ global_context.take_events() };
	zassert_true(fp_events.is_finger_down());
	zassert_true(fp_events.is_image_ready());
}

ZTEST_USER(fpsensor_capture,
	   test_finger_capture_simple_image_scan_success_get_frame)
{
	FpImageFrameParamsCache frame_params_cache;
	frame_params_cache.populate_cache(IMAGE_SIZE);
	constexpr enum fp_capture_type kCaptureType = FP_CAPTURE_SIMPLE_IMAGE;
	const uint32_t image_size =
		*frame_params_cache.get_frame_size(kCaptureType);
	struct ec_params_fp_frame frame_request = {
		.offset = FpFrameV0Offset{ .buffer_index =
						   FP_FRAME_INDEX_RAW_IMAGE }
				  .pack(),
		.size = image_size,
	};

	start_capture_mode(kCaptureType);

	/* Prepare and load image to simulator. */
	image_buffer.fill(1);
	fingerprint_load_image(fp_sim, image_buffer.data(), image_size);

	simulate_finger_touch();

	/* Get fingerprint raw image and compare buffers. */
	zassert_ok(
		ec_cmd_fp_frame(nullptr, &frame_request, frame_buffer.data()));
	zassert_mem_equal(frame_buffer.data(), image_buffer.data(), image_size);
}

ZTEST_USER(fpsensor_capture,
	   test_finger_capture_simple_image_scan_success_get_frame_v1)
{
	FpImageFrameParamsCache frame_params_cache;
	frame_params_cache.populate_cache(IMAGE_SIZE);
	constexpr enum fp_capture_type kCaptureType = FP_CAPTURE_SIMPLE_IMAGE;
	const uint32_t image_size =
		*frame_params_cache.get_frame_size(kCaptureType);
	struct ec_params_fp_frame_v1 frame_request = {
		.cmd = FP_FRAME_GET_RAW_IMAGE,
		.offset = 0,
		.size = image_size,
	};

	start_capture_mode(kCaptureType);

	/* Prepare and load image to simulator. */
	image_buffer.fill(1);
	fingerprint_load_image(fp_sim, image_buffer.data(), image_size);

	simulate_finger_touch();

	/* Get fingerprint raw image and compare buffers. */
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &frame_request,
				      frame_buffer.data()));
	zassert_mem_equal(frame_buffer.data(), image_buffer.data(), image_size);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_get_frame_v1_size_too_big)
{
	std::array<uint8_t, IMAGE_SIZE + 1> buffer{};
	struct ec_params_fp_frame_v1 frame_request = {
		.cmd = FP_FRAME_GET_RAW_IMAGE,
		.offset = 0,
		.size = IMAGE_SIZE + 1,
	};

	start_capture_mode();
	simulate_finger_touch();

	/* Confirm that FP_FRAME host command will return an error. */
	zassert_equal(ec_cmd_fp_frame_v1(nullptr, &frame_request,
					 buffer.data()),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_get_frame_v1_bad_offset)
{
	struct ec_params_fp_frame_v1 frame_request = {
		.cmd = FP_FRAME_GET_RAW_IMAGE,
		.offset = IMAGE_SIZE + 1,
		.size = 1,
	};

	start_capture_mode();
	simulate_finger_touch();

	/* Confirm that FP_FRAME host command will return an error. */
	zassert_equal(ec_cmd_fp_frame_v1(nullptr, &frame_request,
					 frame_buffer.data()),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER(fpsensor_capture,
	   test_finger_capture_get_frame_v1_invalid_capture_type)
{
	struct ec_params_fp_frame_v1 frame_request = {
		.cmd = FP_FRAME_GET_RAW_IMAGE,
		.offset = 0,
		.size = IMAGE_SIZE,
	};

	/* Confirm that FP_FRAME host command will return an error. */
	zassert_equal(ec_cmd_fp_frame_v1(nullptr, &frame_request,
					 frame_buffer.data()),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_pattern0_immediate_success)
{
	struct fingerprint_sensor_state state;

	/* Switch mode to PATTERN0 capture (does not require finger touch). */
	set_fp_mode(FP_MODE_CAPTURE |
		    FpCaptureType::create(FP_CAPTURE_PATTERN0)->to_mode_bits());

	/* Confirm MKBP event was sent with EC_MKBP_FP_IMAGE_READY. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);
	zassert_true(
		FpMkbpEvent{ global_context.take_events() }.is_image_ready());

	/* Confirm that capture mode and capture type bits were cleared. */
	zassert_false(get_fp_mode() &
		      (FP_MODE_CAPTURE | FP_MODE_CAPTURE_TYPE_MASK));
	zassert_equal(global_context.frame.current_capture_type,
		      FP_CAPTURE_PATTERN0);

	fingerprint_get_state(fp_sim, &state);
	zassert_equal(state.last_acquire_image_mode,
		      FINGERPRINT_CAPTURE_TYPE_PATTERN0);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_pattern0_immediate_failure)
{
	update_sim_state([](auto &state) {
		state.acquire_image_result = FINGERPRINT_SENSOR_SCAN_TOO_FAST;
	});

	/* Switch mode to PATTERN0 capture when acquisition fails. */
	set_fp_mode(FP_MODE_CAPTURE |
		    FpCaptureType::create(FP_CAPTURE_PATTERN0)->to_mode_bits());

	/* Confirm MKBP event was sent and current_capture_type is invalid. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_true(
		FpMkbpEvent{ global_context.take_events() }.is_image_ready());
	zassert_false(global_context.frame.current_capture_type.has_value());
}

ZTEST_USER(fpsensor_capture, test_deepsleep_mode)
{
	struct fingerprint_sensor_state state;

	set_fp_mode(FP_MODE_DEEPSLEEP | FP_MODE_FINGER_DOWN);

	fingerprint_get_state(fp_sim, &state);
	zassert_true(state.low_power_mode);
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
	set_fp_mode(0);
	set_fp_mode(FP_MODE_RESET_SENSOR);

	reset_sim_state();
	RESET_FAKE(mkbp_send_event);
}

ZTEST_SUITE(fpsensor_capture, nullptr, fpsensor_setup, fpsensor_before, nullptr,
	    nullptr);
