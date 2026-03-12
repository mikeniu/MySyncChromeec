/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>

#include <fpsensor/fpsensor_led.h>

LOG_MODULE_REGISTER(fp_led, LOG_LEVEL_INF);

namespace
{

constexpr uint32_t perceivedToEmitted(float perceived, int gamma = 3)
{
	// Constrain input
	float p = (perceived < 0.0f) ?
			  0.0f :
			  ((perceived > 100.0f) ? 100.0f : perceived);
	// Normalize perceived brightness to a 0.0 - 1.0 range
	float normalized = p / 100.0f;
	// Apply the gamma power curve
	float curve_value = 1.0f;
	for (int i = 0; i < gamma; ++i) {
		curve_value *= normalized;
	}
	// Scale to 100% and round to nearest integer
	return static_cast<uint32_t>(curve_value * 100 + 0.5f);
}

constexpr uint32_t BRIGHTNESS_ENROLL = perceivedToEmitted(70);
constexpr uint32_t BRIGHTNESS_MATCH = perceivedToEmitted(70);
constexpr uint32_t BRIGHTNESS_MATCH_OK = perceivedToEmitted(100);
constexpr uint32_t BRIGHTNESS_MATCH_NOK = perceivedToEmitted(40);
constexpr uint32_t BRIGHTNESS_OFF = perceivedToEmitted(0);

constexpr k_timeout_t DURATION_MATCH = K_SECONDS(10);
constexpr k_timeout_t DURATION_MATCH_OK = K_SECONDS(2);
constexpr k_timeout_t DURATION_MATCH_NOK = K_SECONDS(1);

const pwm_dt_spec pwms = PWM_DT_SPEC_GET(DT_ALIAS(pwm_fp_led));

enum class Mode {
	OFF,
	ENROLL,
	MATCH,
	MATCH_OK,
	MATCH_NOK,
};

Mode led_mode;

K_MUTEX_DEFINE(led_mutex);

void set_led_brightness(uint32_t percentage)
{
	uint32_t pulse = (pwms.period * percentage) / 100;
	pwm_set_pulse_dt(&pwms, pulse);
}

void led_update_handler(struct k_work *work)
{
	k_mutex_lock(&led_mutex, K_FOREVER);
	if (led_mode == Mode::MATCH || led_mode == Mode::MATCH_OK ||
	    led_mode == Mode::MATCH_NOK) {
		led_mode = Mode::OFF;
		set_led_brightness(BRIGHTNESS_OFF);
	}
	k_mutex_unlock(&led_mutex);
}

K_WORK_DELAYABLE_DEFINE(led_update_data, led_update_handler);

void set_led(uint32_t brightness)
{
	k_work_cancel_delayable(&led_update_data);
	set_led_brightness(brightness);
}

void set_led_with_timeout(uint32_t brightness, k_timeout_t time)
{
	set_led(brightness);
	k_work_schedule(&led_update_data, time);
}

int fp_led_init()
{
	if (!pwm_is_ready_dt(&pwms)) {
		LOG_ERR("PWM is not ready");
		return -ENODEV;
	}
	return 0;
}

void enroll(void)
{
	k_mutex_lock(&led_mutex, K_FOREVER);
	if (led_mode == Mode::ENROLL) {
		k_mutex_unlock(&led_mutex);
		return;
	}
	led_mode = Mode::ENROLL;
	set_led(BRIGHTNESS_ENROLL);
	k_mutex_unlock(&led_mutex);

	LOG_INF("Fingerprint led enroll.");
}

void match(void)
{
	k_mutex_lock(&led_mutex, K_FOREVER);
	if (led_mode == Mode::MATCH) {
		k_mutex_unlock(&led_mutex);
		return;
	}
	led_mode = Mode::MATCH;
	set_led_with_timeout(BRIGHTNESS_MATCH, DURATION_MATCH);
	k_mutex_unlock(&led_mutex);

	LOG_INF("Fingerprint led match.");
}

void match_ok(void)
{
	k_mutex_lock(&led_mutex, K_FOREVER);
	led_mode = Mode::MATCH_OK;
	set_led_with_timeout(BRIGHTNESS_MATCH_OK, DURATION_MATCH_OK);
	k_mutex_unlock(&led_mutex);

	LOG_INF("Fingerprint led match OK.");
}

void match_nok(void)
{
	k_mutex_lock(&led_mutex, K_FOREVER);
	led_mode = Mode::MATCH_NOK;
	set_led_with_timeout(BRIGHTNESS_MATCH_NOK, DURATION_MATCH_NOK);
	k_mutex_unlock(&led_mutex);

	LOG_INF("Fingerprint led match NOK.");
}

void off(void)
{
	k_mutex_lock(&led_mutex, K_FOREVER);
	if (led_mode == Mode::OFF || led_mode == Mode::MATCH_OK ||
	    led_mode == Mode::MATCH_NOK) {
		k_mutex_unlock(&led_mutex);
		return;
	}
	set_led(BRIGHTNESS_OFF);
	led_mode = Mode::OFF;
	k_mutex_unlock(&led_mutex);

	LOG_INF("Fingerprint led off.");
}

} // namespace

namespace fp_led
{

void update_mode(uint32_t sensor_mode)
{
	if (sensor_mode & FP_MODE_ENROLL_SESSION)
		enroll();
	else if (sensor_mode & FP_MODE_MATCH)
		match();
	else
		off();
}

void update_match(bool success)
{
	if (success)
		match_ok();
	else
		match_nok();
}

} // namespace fp_led

SYS_INIT(fp_led_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
