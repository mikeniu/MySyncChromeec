/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
#include <fpsensor/fpsensor_led.h>
#include <fpsensor/fpsensor_platform.h>
#include <limits>

LOG_MODULE_REGISTER(fp_led, LOG_LEVEL_INF);

namespace
{

#define LED_NODE DT_ALIAS(pwm_fp_led)
const struct device *const led_dev = DEVICE_DT_GET(DT_PARENT(LED_NODE));
constexpr uint32_t led_idx = DT_NODE_CHILD_IDX(LED_NODE);
bool pm_locked = false;

void set_led_brightness(uint8_t percentage)
{
	if (percentage > 0) {
		if (!pm_locked) {
			pm_policy_state_all_lock_get();
			pm_locked = true;
		}
#ifdef CONFIG_PM_DEVICE
		pm_device_action_run(led_dev, PM_DEVICE_ACTION_RESUME);
#endif
	}
	led_set_brightness(led_dev, led_idx, percentage);
	if (percentage == 0) {
#ifdef CONFIG_PM_DEVICE
		pm_device_action_run(led_dev, PM_DEVICE_ACTION_SUSPEND);
#endif
		if (pm_locked) {
			pm_policy_state_all_lock_put();
			pm_locked = false;
		}
	}
}

int init()
{
	if (!device_is_ready(led_dev)) {
		LOG_ERR("LED device is not ready");
		return -ENODEV;
	}
	set_led_brightness(0);
	return 0;
}
SYS_INIT(init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

}

#ifdef CONFIG_CROS_EC_RW
namespace
{

consteval uint8_t perceivedToEmitted(float perceived, int gamma = 3)
{
	/* Normalize clamped perceived brightness to a 0.0 - 1.0 range */
	float normalized = std::clamp(perceived, 0.0f, 100.0f) / 100.0f;
	/* Apply the gamma power curve */
	float curve_value = 1.0f;
	for (int i = 0; i < gamma; ++i) {
		curve_value *= normalized;
	}
	/* Scale to 100% and round to nearest integer */
	return static_cast<uint8_t>(curve_value * 100 + 0.5f);
}

constexpr uint8_t BRIGHTNESS_ENROLL =
	perceivedToEmitted(CONFIG_PLATFORM_EC_FP_LED_BRIGHTNESS_ENROLL);
constexpr uint8_t BRIGHTNESS_MATCH =
	perceivedToEmitted(CONFIG_PLATFORM_EC_FP_LED_BRIGHTNESS_MATCH);
constexpr uint8_t BRIGHTNESS_MATCH_OK =
	perceivedToEmitted(CONFIG_PLATFORM_EC_FP_LED_BRIGHTNESS_MATCH_OK);
constexpr uint8_t BRIGHTNESS_MATCH_NOK =
	perceivedToEmitted(CONFIG_PLATFORM_EC_FP_LED_BRIGHTNESS_MATCH_NOK);
constexpr uint8_t BRIGHTNESS_OFF = perceivedToEmitted(0);

enum class Mode {
	OFF,
	ENROLL,
	MATCH,
	MATCH_OK,
	MATCH_NOK,
};

Mode led_mode;
int64_t timer_scheduled_at_ticks = 0;

K_SEM_DEFINE(led_lock, 1, 1);

[[nodiscard]] constexpr bool ignore_off(Mode mode) noexcept
{
	return mode == Mode::OFF || mode == Mode::MATCH_OK ||
	       mode == Mode::MATCH_NOK;
}

void led_update_handler(struct k_work *work)
{
	fp_platform::ScopedSemLock lock(led_lock);
	bool is_timer_expired = k_uptime_ticks() >= timer_scheduled_at_ticks;
	if (is_timer_expired) {
		led_mode = Mode::OFF;
		set_led_brightness(BRIGHTNESS_OFF);
	}
}

K_WORK_DELAYABLE_DEFINE(led_update_dwork, led_update_handler);

void set_led(uint8_t brightness)
{
	/* Always called with led_lock taken */
	int ret = k_work_cancel_delayable(&led_update_dwork);
	if (ret < 0) {
		LOG_ERR("Failed to cancel LED update work: %d", ret);
	}
	/* In case the work was not cancelled, set the timer to max to prevent
	 * the handler from turning the LED off */
	timer_scheduled_at_ticks = std::numeric_limits<int64_t>::max();
	set_led_brightness(brightness);
}

void set_led_with_timeout(uint8_t brightness, std::chrono::milliseconds delay)
{
	/* Always called with led_lock taken */
	set_led_brightness(brightness);
	timer_scheduled_at_ticks =
		k_uptime_ticks() + k_ms_to_ticks_floor64(delay.count());
	int ret = k_work_reschedule(&led_update_dwork, K_MSEC(delay.count()));
	if (ret < 0) {
		LOG_ERR("Failed to reschedule LED update work: %d", ret);
		/* Fallback to turning LED off */
		set_led_brightness(BRIGHTNESS_OFF);
	}
}

void enroll()
{
	{
		fp_platform::ScopedSemLock lock(led_lock);
		if (led_mode == Mode::ENROLL) {
			return;
		}
		led_mode = Mode::ENROLL;
		set_led(BRIGHTNESS_ENROLL);
	}

	LOG_INF("Fingerprint led enroll.");
}

void match()
{
	{
		fp_platform::ScopedSemLock lock(led_lock);
		if (led_mode == Mode::MATCH) {
			return;
		}
		led_mode = Mode::MATCH;
		set_led_with_timeout(
			BRIGHTNESS_MATCH,
			std::chrono::milliseconds(
				CONFIG_PLATFORM_EC_FP_LED_DURATION_MATCH_MS));
	}

	LOG_INF("Fingerprint led match.");
}

void off()
{
	{
		fp_platform::ScopedSemLock lock(led_lock);
		if (ignore_off(led_mode)) {
			return;
		}
		set_led(BRIGHTNESS_OFF);
		led_mode = Mode::OFF;
	}

	LOG_INF("Fingerprint led off.");
}

} /* namespace */

namespace fp_led
{

void update_mode(FpSensorMode sensor_mode)
{
	if (sensor_mode.has_any(FP_MODE_ENROLL_SESSION))
		enroll();
	else if (sensor_mode.has_any(FP_MODE_MATCH))
		match();
	else
		off();
}

void update_match(bool success)
{
	{
		fp_platform::ScopedSemLock lock(led_lock);
		led_mode = success ? Mode::MATCH_OK : Mode::MATCH_NOK;
		set_led_with_timeout(
			success ? BRIGHTNESS_MATCH_OK : BRIGHTNESS_MATCH_NOK,
			std::chrono::milliseconds(
				success ?
					CONFIG_PLATFORM_EC_FP_LED_DURATION_MATCH_OK_MS :
					CONFIG_PLATFORM_EC_FP_LED_DURATION_MATCH_NOK_MS));
	}

	LOG_INF("Fingerprint led match %s.", success ? "OK" : "NOK");
}

} /* namespace fp_led */
#endif /* CONFIG_CROS_EC_RW */