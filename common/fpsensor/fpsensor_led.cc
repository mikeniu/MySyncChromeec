/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor_led.h"
#include "fpsensor/fpsensor_platform.h"

#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/clock.h>

#include <algorithm>
#include <chrono>
#include <limits>

#pragma GCC poison global_context

LOG_MODULE_REGISTER(fp_led, LOG_LEVEL_INF);

namespace fp_led
{

namespace
{

#define LED_NODE DT_ALIAS(pwm_fp_led)
	const device *const kLedDev = DEVICE_DT_GET(DT_PARENT(LED_NODE));
	constexpr uint32_t kLedIdx = DT_NODE_CHILD_IDX(LED_NODE);

	FpLedController controller;

	int init_fp_led()
	{
		return controller.init();
	}
	SYS_INIT(init_fp_led, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

} /* namespace */

void FpLedController::set_brightness(uint8_t percentage)
{
	if (percentage > 0) {
		if (!pm_locked_) {
			pm_policy_state_all_lock_get();
			pm_locked_ = true;
		}
#ifdef CONFIG_PM_DEVICE
		pm_device_action_run(kLedDev, PM_DEVICE_ACTION_RESUME);
#endif
	}
	led_set_brightness(kLedDev, kLedIdx, percentage);
	if (percentage == 0) {
#ifdef CONFIG_PM_DEVICE
		pm_device_action_run(kLedDev, PM_DEVICE_ACTION_SUSPEND);
#endif
		if (pm_locked_) {
			pm_policy_state_all_lock_put();
			pm_locked_ = false;
		}
	}
}

int FpLedController::init()
{
	if (!device_is_ready(kLedDev)) {
		LOG_ERR("LED device is not ready");
		return -ENODEV;
	}
	set_brightness(0);
	return 0;
}

#ifdef CONFIG_CROS_EC_RW
namespace
{

	consteval uint8_t perceived_to_emitted(float perceived, int gamma = 3)
	{
		/* Normalize clamped perceived brightness to a 0.0 - 1.0 range
		 */
		float normalized = std::clamp(perceived, 0.0f, 100.0f) / 100.0f;
		/* Apply the gamma power curve */
		float curve_value = 1.0f;
		for (int i = 0; i < gamma; ++i) {
			curve_value *= normalized;
		}
		/* Scale to 100% and round to nearest integer */
		return static_cast<uint8_t>(curve_value * 100 + 0.5f);
	}

	constexpr uint8_t kBrightnessEnroll = perceived_to_emitted(
		CONFIG_PLATFORM_EC_FP_LED_BRIGHTNESS_ENROLL);
	constexpr uint8_t kBrightnessMatch = perceived_to_emitted(
		CONFIG_PLATFORM_EC_FP_LED_BRIGHTNESS_MATCH);
	constexpr uint8_t kBrightnessMatchOk = perceived_to_emitted(
		CONFIG_PLATFORM_EC_FP_LED_BRIGHTNESS_MATCH_OK);
	constexpr uint8_t kBrightnessMatchNok = perceived_to_emitted(
		CONFIG_PLATFORM_EC_FP_LED_BRIGHTNESS_MATCH_NOK);
	constexpr uint8_t kBrightnessOff = perceived_to_emitted(0);

	constexpr std::chrono::milliseconds kDurationMatch{
		CONFIG_PLATFORM_EC_FP_LED_DURATION_MATCH_MS
	};
	constexpr std::chrono::milliseconds kDurationMatchOk{
		CONFIG_PLATFORM_EC_FP_LED_DURATION_MATCH_OK_MS
	};
	constexpr std::chrono::milliseconds kDurationMatchNok{
		CONFIG_PLATFORM_EC_FP_LED_DURATION_MATCH_NOK_MS
	};

	K_SEM_DEFINE(led_lock, 1, 1);

	[[nodiscard]] constexpr bool
	ignore_off(FpLedController::Mode mode) noexcept
	{
		return mode == FpLedController::Mode::kOff ||
		       mode == FpLedController::Mode::kMatchOk ||
		       mode == FpLedController::Mode::kMatchNok;
	}

	void led_update_handler(k_work *)
	{
		controller.on_timeout();
	}

	K_WORK_DELAYABLE_DEFINE(led_update_dwork, led_update_handler);

} /* namespace */

void FpLedController::on_timeout()
{
	fp_platform::ScopedSemLock lock(led_lock);
	const bool is_timer_expired = k_uptime_ticks() >=
				      timer_scheduled_at_ticks_;
	if (is_timer_expired) {
		mode_ = Mode::kOff;
		set_brightness(kBrightnessOff);
	}
}

void FpLedController::set_led(uint8_t brightness)
{
	/* Always called with led_lock taken */
	const int ret = k_work_cancel_delayable(&led_update_dwork);
	if (ret < 0) {
		LOG_ERR("Failed to cancel LED update work: %d", ret);
	}
	/* In case the work was not cancelled, set the timer to max to prevent
	 * the handler from turning the LED off */
	timer_scheduled_at_ticks_ = std::numeric_limits<int64_t>::max();
	set_brightness(brightness);
}

void FpLedController::set_led_with_timeout(uint8_t brightness,
					   std::chrono::milliseconds delay)
{
	/* Always called with led_lock taken */
	set_brightness(brightness);
	timer_scheduled_at_ticks_ =
		k_uptime_ticks() + k_ms_to_ticks_floor64(delay.count());
	const int ret =
		k_work_reschedule(&led_update_dwork, K_MSEC(delay.count()));
	if (ret < 0) {
		LOG_ERR("Failed to reschedule LED update work: %d", ret);
		/* Fallback to turning LED off */
		set_brightness(kBrightnessOff);
	}
}

void FpLedController::enroll()
{
	{
		fp_platform::ScopedSemLock lock(led_lock);
		if (mode_ == Mode::kEnroll) {
			return;
		}
		mode_ = Mode::kEnroll;
		set_led(kBrightnessEnroll);
	}

	LOG_INF("Fingerprint led enroll.");
}

void FpLedController::match()
{
	{
		fp_platform::ScopedSemLock lock(led_lock);
		if (mode_ == Mode::kMatch) {
			return;
		}
		mode_ = Mode::kMatch;
		set_led_with_timeout(kBrightnessMatch, kDurationMatch);
	}

	LOG_INF("Fingerprint led match.");
}

void FpLedController::off()
{
	{
		fp_platform::ScopedSemLock lock(led_lock);
		if (ignore_off(mode_)) {
			return;
		}
		set_led(kBrightnessOff);
		mode_ = Mode::kOff;
	}

	LOG_INF("Fingerprint led off.");
}

void FpLedController::update_mode(FpSensorMode sensor_mode)
{
	if (sensor_mode.has_any(FP_MODE_ENROLL_SESSION)) {
		enroll();
	} else if (sensor_mode.has_any(FP_MODE_MATCH)) {
		match();
	} else {
		off();
	}
}

void FpLedController::update_match(bool success)
{
	{
		fp_platform::ScopedSemLock lock(led_lock);
		mode_ = success ? Mode::kMatchOk : Mode::kMatchNok;
		set_led_with_timeout(
			success ? kBrightnessMatchOk : kBrightnessMatchNok,
			success ? kDurationMatchOk : kDurationMatchNok);
	}

	LOG_INF("Fingerprint led match %s.", success ? "OK" : "NOK");
}

void update_mode(FpSensorMode sensor_mode)
{
	controller.update_mode(sensor_mode);
}

void update_match(bool success)
{
	controller.update_match(success);
}

#endif /* CONFIG_CROS_EC_RW */

} /* namespace fp_led */