/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_MACHINE_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_MACHINE_H_

#include "ec_commands.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_platform.h"
#include "fpsensor/fpsensor_state.h"
#include "timer.h"

#include <cstdint>
#include <variant>

class FpSensorStateMachine {
    public:
	static constexpr int kFingerPollingDelayUs = 100 * MSEC;

	struct IdleCommand {};
	struct ImmediateCaptureCommand {
		FpCaptureType capture_type{};
	};
	struct DetectFingerCommand {
		uint32_t mode = 0;
	};
	struct ResetSensorCommand {};
	struct MaintenanceCommand {};
	struct EncryptTemplateCommand {};
	struct DecryptTemplateCommand {};

	using Command = std::variant<IdleCommand, ImmediateCaptureCommand,
				     DetectFingerCommand, ResetSensorCommand,
				     MaintenanceCommand, EncryptTemplateCommand,
				     DecryptTemplateCommand>;

	explicit constexpr FpSensorStateMachine(FpSensorContext &ctx) noexcept
		: ctx_(ctx)
	{
	}

	[[nodiscard]] static constexpr Command
	parse_command(FpSensorMode mode) noexcept
	{
		if (!mode.is_finger_needed())
			return ImmediateCaptureCommand{ mode.capture_type() };
		if (mode.is_detect_finger())
			return DetectFingerCommand{ mode.raw() };
		if (mode.has_any(FP_MODE_RESET_SENSOR))
			return ResetSensorCommand{};
		if (mode.has_any(FP_MODE_SENSOR_MAINTENANCE))
			return MaintenanceCommand{};
		if (mode.has_any(FP_MODE_ENCRYPT_TEMPLATE))
			return EncryptTemplateCommand{};
		if (mode.has_any(FP_MODE_DECRYPT_TEMPLATE))
			return DecryptTemplateCommand{};
		return IdleCommand{};
	}

	[[nodiscard]] constexpr int timeout_us() const noexcept
	{
		return timeout_us_;
	}

	void handle_event(FpTaskEvent evt);

    private:
#ifdef HAVE_FP_PRIVATE_DRIVER
	void handle_mode_change();
	void execute_command(IdleCommand)
	{
		fp_platform::enter_sensor_low_power();
	}
	void execute_command(ImmediateCaptureCommand cmd);
	void execute_command(DetectFingerCommand cmd)
	{
		fp_configure_detect();
		if (cmd.mode & FP_MODE_DEEPSLEEP)
			fp_platform::enter_sensor_low_power();
		timeout_us_ = (cmd.mode & FP_MODE_FINGER_UP) ?
				      kFingerPollingDelayUs :
				      -1;
		if (cmd.mode & FP_MODE_ANY_WAIT_IRQ)
			fp_platform::enable_sensor_irq();
		else
			fp_platform::enter_sensor_low_power();
	}
	void execute_command(ResetSensorCommand)
	{
		fp_reset_and_clear_context();
		ctx_.sensor_mode &= ~FP_MODE_RESET_SENSOR;
	}
	void execute_command(MaintenanceCommand)
	{
		fp_maintenance();
		ctx_.sensor_mode &= ~FP_MODE_SENSOR_MAINTENANCE;
	}
	void execute_command(EncryptTemplateCommand)
	{
		ctx_.encrypt_requested_template();
	}
	void execute_command(DecryptTemplateCommand)
	{
		ctx_.decrypt_and_commit_template();
	}
#endif

	FpSensorContext &ctx_;
	int timeout_us_ = -1;
};

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_STATE_MACHINE_H_ */
