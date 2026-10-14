/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor task state machine */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_MACHINE_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_MACHINE_H_

#include "common.h"
#include "fpsensor_types.h"
#include "timer.h"

#include <stdint.h>

struct FpSensorStats {
	uint32_t capture_time_us{};
	uint32_t matching_time_us{};
	uint32_t overall_time_us{};
	timestamp_t overall_t0{};
	uint8_t timestamps_invalid{};
	/*
	 * Last matched template index, persisted for telemetry
	 * (EC_CMD_FP_STATS). Unlike global_context, this is not cleared when
	 * the secret is read.
	 */
	int8_t template_matched{};
};

class FpSensorStateMachine {
    public:
	void run();
	void handle_event(uint32_t evt);
	[[nodiscard]] const FpSensorStats &stats() const noexcept;

    private:
	static constexpr int kWaitForeverUs = -1;
	static constexpr int kFingerPollingDelayUs = 100 * MSEC;

	void init();
	[[nodiscard]] static bool has_active_mode(uint32_t flags) noexcept;
	static void clear_active_mode(uint32_t flags) noexcept;

#ifdef HAVE_FP_PRIVATE_DRIVER
	void handle_config_update();
	void update_enroll_session();
	void acquire_immediate_image(fp_capture_type capture_type);
	void configure_sensor_for_mode();
	void execute_mode_action();
	void encrypt_requested_template();
	void decrypt_loaded_template();

	void handle_sensor_or_timer_event(uint32_t evt);
	finger_state check_finger_presence();
	void process_finger_capture();
	uint32_t process_enroll();
	uint32_t process_match();
	void end_enroll_session();
	static void update_peripherals();
#endif /* HAVE_FP_PRIVATE_DRIVER */

	FpSensorStats stats_{};
	int timeout_us_ = kWaitForeverUs;
#ifdef HAVE_FP_PRIVATE_DRIVER
	bool enroll_session_active_ = false;
#endif
};

extern FpSensorStateMachine fp_state_machine;

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_MACHINE_H_ */
