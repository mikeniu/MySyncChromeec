/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"
#include "compile_time_macros.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_btn_ign_out.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_detect.h"
#include "fpsensor/fpsensor_led.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_platform.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_types.h"
#include "fpsensor/fpsensor_utils.h"
#include "mkbp_event.h"
#include "scoped_fast_cpu.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#include <cstdint>
#include <optional>

#if !defined(CONFIG_RNG)
#error "fpsensor requires RNG"
#endif

#if defined(CONFIG_CROS_EC_RO)
#error "fpsensor code should not be in RO image."
#endif

/* Delay between 2 s of the sensor to detect finger removal */
#define FINGER_POLLING_DELAY (100 * MSEC)

BUILD_ASSERT(sizeof(struct ec_fp_template_encryption_metadata) % 4 == 0);

extern FpSensorContext global_context;

static void send_mkbp_event(FpMkbpEvent event)
{
	global_context.events.add(event);
	mkbp_send_event(EC_MKBP_EVENT_FINGERPRINT);
}

#ifdef HAVE_FP_PRIVATE_DRIVER

/*
 * contains the bit FP_MODE_ENROLL_SESSION if a finger enrollment is on-going.
 * It is used to detect the ENROLL_SESSION transition when sensor_mode is
 * updated by the host.
 */
static uint32_t enroll_session;

static FpMkbpEvent fp_process_enroll(void)
{
	int percent = 0;

	/* Prevent enrollment if we have reached max capacity. */
	const auto enroll_id = global_context.templates.next_free_id();
	if (!enroll_id.has_value()) {
		CPRINTS("Error: Max templates reached.");
		fp_enrollment_finish(nullptr);
		global_context.sensor_mode =
			FpSensorMode(global_context.sensor_mode.raw() &
				     ~FP_MODE_ENROLL_SESSION);
		enroll_session &= ~FP_MODE_ENROLL_SESSION;
		return FpMkbpEvent::enroll(EC_MKBP_FP_ERR_ENROLL_INTERNAL, 0);
	}

	if (global_context.templates.template_newly_enrolled.has_value())
		CPRINTS("Warning: previously enrolled template has not been "
			"read yet.");

	/* begin/continue enrollment */
	CPRINTS("[%d]Enrolling ...", enroll_id->raw());
	int res = fp_finger_enroll(global_context.frame.image_buffer.data(),
				   &percent);
	CPRINTS("[%d]Enroll =>%d (%d%%)", enroll_id->raw(), res, percent);
	if (res < 0)
		return FpMkbpEvent::enroll(EC_MKBP_FP_ERR_ENROLL_INTERNAL, 0);
	global_context.templates.mark_dirty(*enroll_id);
	if (percent == 100) {
		res = fp_enrollment_finish(
			global_context.templates.templates[enroll_id->raw()]
				.data());
		if (res) {
			res = EC_MKBP_FP_ERR_ENROLL_INTERNAL;
		} else {
			global_context.templates.finish_enrollment(
				*enroll_id,
				global_context.crypto.auth_enabled());
		}
		global_context.sensor_mode =
			FpSensorMode(global_context.sensor_mode.raw() &
				     ~FP_MODE_ENROLL_SESSION);
		enroll_session &= ~FP_MODE_ENROLL_SESSION;
	}
	return FpMkbpEvent::enroll(res, percent);
}

static FpMkbpEvent fp_process_match(void)
{
	const FpTimestamp t0 = FpClock::now();
	const FpMatchResult result = global_context.templates.match(
		global_context.frame.image_buffer,
		!global_context.sensor_mode.is_match_no_template_update());

	fp_led::update_match(result.event.is_match_success());
	global_context.stats.record_match(t0, result.matched_id, result.valid);
	return result.event;
}

static void fp_process_finger(void)
{
	const FpTimestamp t0 = FpClock::now();
	const FpCaptureType capture_type =
		global_context.sensor_mode.capture_type();
	global_context.frame.begin_capture();

	CPRINTS("Capturing ...");
	const int res = fp_acquire_image(
		global_context.frame.image_buffer.data(), capture_type.raw());
	global_context.stats.record_capture(t0, res == 0);
	if (res != 0)
		return;

	global_context.frame.finish_capture(capture_type);
	FpMkbpEvent evt = FpMkbpEvent::image_ready();

	fp_platform::flush_spi_after_acquire();

	/* we need CPU power to do the computations */
	ScopedFastCpu fast_cpu;

	if (global_context.sensor_mode.is_enroll_image())
		evt = fp_process_enroll();
	else if (global_context.sensor_mode.is_match())
		evt = fp_process_match();

	global_context.sensor_mode =
		FpSensorMode(global_context.sensor_mode.raw() &
			     ~(FP_MODE_ANY_CAPTURE | FP_MODE_CAPTURE_TYPE_MASK |
			       FP_MODE_MATCH_NO_TEMPLATE_UPDATE));
	global_context.stats.finish_overall();
	send_mkbp_event(evt);
}
#endif /* HAVE_FP_PRIVATE_DRIVER */

extern "C" void fp_task(void)
{
	int timeout_us = -1;
	__maybe_unused bool pm_locked = true;

	/* Lock PM for initialization. */
	fp_platform::lock_pm();
	CPRINTS("FP_SENSOR_SEL: %s",
		fp_sensor_type_to_str(fpsensor_detect_get_type()));

#ifdef HAVE_FP_PRIVATE_DRIVER
	/* Reset and initialize the sensor IC */
	fp_sensor_init();

	global_context.frame.init_cache();

	while (1) {
		enum finger_state st = FINGER_NONE;

		/* Unlock PM while waiting for an event except for an
		 * enrollment process.
		 */
		if (!global_context.sensor_mode.is_enroll_session()) {
			fp_platform::unlock_pm();
			pm_locked = false;
		}
		/* Wait for a sensor IRQ or a new mode configuration */
		uint32_t evt = task_wait_event(timeout_us);

		/* Lock PM for any FP related actions, especially communication
		 * with a FP sensor.
		 */
		if (!pm_locked) {
			fp_platform::lock_pm();
			pm_locked = true;
		}

		if (evt & TASK_EVENT_UPDATE_CONFIG) {
			uint32_t mode = global_context.sensor_mode.raw();
			fp_platform::prepare_for_mode_change();
			if ((mode ^ enroll_session) & FP_MODE_ENROLL_SESSION) {
				int ret = 0;
				if (mode & FP_MODE_ENROLL_SESSION) {
					ret = fp_enrollment_begin();
					if (ret)
						global_context
							.sensor_mode = FpSensorMode(
							global_context
								.sensor_mode
								.raw() &
							~FP_MODE_ENROLL_SESSION);
				} else {
					fp_enrollment_finish(nullptr);
				}
				enroll_session =
					ret ? 0 :
					      (mode & FP_MODE_ENROLL_SESSION);
			}
			if (!global_context.sensor_mode.is_finger_needed()) {
				const FpCaptureType capture_type =
					global_context.sensor_mode
						.capture_type();
				global_context.frame.begin_capture();
				if (!fp_acquire_image(
					    global_context.frame.image_buffer
						    .data(),
					    capture_type.raw())) {
					global_context.frame.finish_capture(
						capture_type);
				}
				global_context.sensor_mode = FpSensorMode(
					global_context.sensor_mode.raw() &
					~(FP_MODE_CAPTURE |
					  FP_MODE_CAPTURE_TYPE_MASK));
				send_mkbp_event(FpMkbpEvent::image_ready());
				continue;
			} else if (global_context.sensor_mode
					   .is_detect_finger()) {
				/* wait for a finger on the sensor */
				fp_configure_detect();
			}
			if (global_context.sensor_mode.is_deepsleep())
				/* Shutdown the sensor */
				fp_sensor_low_power();
			if (global_context.sensor_mode.is_finger_up())
				/* Poll the sensor to detect finger removal */
				timeout_us = FINGER_POLLING_DELAY;
			else
				timeout_us = -1;
			if (mode & FP_MODE_ANY_WAIT_IRQ) {
				fp_platform::enable_sensor_irq();
			} else if (mode & FP_MODE_RESET_SENSOR) {
				global_context.reset_and_clear_context();
				global_context.sensor_mode = FpSensorMode(
					global_context.sensor_mode.raw() &
					~FP_MODE_RESET_SENSOR);
			} else if (mode & FP_MODE_SENSOR_MAINTENANCE) {
				fp_maintenance();
				global_context.sensor_mode = FpSensorMode(
					global_context.sensor_mode.raw() &
					~FP_MODE_SENSOR_MAINTENANCE);
			} else if (mode & FP_MODE_ENCRYPT_TEMPLATE) {
				global_context.encrypt_requested_template();
				global_context.sensor_mode = FpSensorMode(
					global_context.sensor_mode.raw() &
					~FP_MODE_ENCRYPT_TEMPLATE);
			} else if (mode & FP_MODE_DECRYPT_TEMPLATE) {
				global_context.decrypt_and_commit_template();
				global_context.sensor_mode = FpSensorMode(
					global_context.sensor_mode.raw() &
					~FP_MODE_DECRYPT_TEMPLATE);
			} else {
				fp_sensor_low_power();
			}
		} else if (evt & (TASK_EVENT_SENSOR_IRQ | TASK_EVENT_TIMER)) {
			global_context.stats.start_overall();
			fp_platform::prepare_for_sensor_or_timer_event(
				FpTaskEvent(evt));
			if (global_context.sensor_mode.is_detect_finger()) {
				st = fp_finger_status();
				if (st == FINGER_PRESENT &&
				    global_context.sensor_mode.is_finger_down()) {
					CPRINTS("Finger!");
					global_context
						.sensor_mode = FpSensorMode(
						global_context.sensor_mode.raw() &
						~FP_MODE_FINGER_DOWN);
					send_mkbp_event(
						FpMkbpEvent::finger_down());
				}
				if (st == FINGER_NONE &&
				    global_context.sensor_mode.is_finger_up()) {
					global_context
						.sensor_mode = FpSensorMode(
						global_context.sensor_mode.raw() &
						~FP_MODE_FINGER_UP);
					timeout_us = -1;
					send_mkbp_event(
						FpMkbpEvent::finger_up());
				}
			}

			if (st == FINGER_PRESENT &&
			    global_context.sensor_mode.has_any(
				    FP_MODE_ANY_CAPTURE)) {
				CPRINTS("Finger down (capture mode)!");
				/*
				 * Send FINGER_DOWN immediately upon detection
				 * during a capture session so the host can
				 * accurately measure interaction latency.
				 */
				send_mkbp_event(FpMkbpEvent::finger_down());
				fp_process_finger();
			}

			if (global_context.sensor_mode.has_any(
				    FP_MODE_ANY_WAIT_IRQ)) {
				fp_configure_detect();
				fp_platform::enable_sensor_irq();
			} else {
				fp_platform::enter_sensor_low_power(
					FpTaskEvent(evt));
			}
		}
		fp_btn_ign_out::update(global_context.sensor_mode);
		fp_led::update_mode(global_context.sensor_mode);
	}
#else /* !HAVE_FP_PRIVATE_DRIVER */
	while (1) {
		uint32_t evt = task_wait_event(timeout_us);

		send_mkbp_event(FpMkbpEvent{ evt });
	}
#endif /* !HAVE_FP_PRIVATE_DRIVER */
}
