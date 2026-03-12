/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#define DT_DRV_COMPAT cros_ec_fp_led
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>

#include <fpsensor/fpsensor_led.h>

LOG_MODULE_REGISTER(fp_led, LOG_LEVEL_INF);

namespace
{

constexpr uint32_t BRIGHTNESS_ENROLL = 70;
constexpr uint32_t BRIGHTNESS_MATCH = 70;
constexpr uint32_t BRIGHTNESS_MATCH_OK_LOW = 10;
constexpr uint32_t BRIGHTNESS_MATCH_OK_HIGH = 70;
constexpr uint32_t BRIGHTNESS_FAIL = 10;
constexpr uint32_t BRIGHTNESS_OFF = 0;

constexpr k_timeout_t DURATION_ENROLL = K_SECONDS(30);
constexpr k_timeout_t DURATION_MATCH = K_SECONDS(30);
constexpr k_timeout_t DURATION_MATCH_OK_PERIOD = K_MSEC(500);
constexpr k_timeout_t DURATION_FAIL = K_SECONDS(2);

struct fp_led_config {
	pwm_dt_spec pwms;
};

const fp_led_config cfg = {
	.pwms = PWM_DT_SPEC_GET(DT_ALIAS(pwm_fp_led)),
};

enum class Mode {
	OFF,
	ENROLL,
	MATCH,
	MATCH_OK,
	MATCH_NOK,
};

struct {
	k_mutex mutex;
	uint32_t next_brightness;
	uint32_t previous_brightness;
	uint32_t final_brightness;
	uint32_t blink_repeat;
	k_timeout_t blink_period;
	Mode mode;
} led_state;

k_work_delayable led_update_work;

void set_led_brightness(uint32_t percentage)
{
	uint32_t pulse = (cfg.pwms.period * percentage) / 100;
	pwm_set_pulse_dt(&cfg.pwms, pulse);
}

void led_update_handler(struct k_work *work)
{
	k_mutex_lock(&led_state.mutex, K_FOREVER);

	if (led_state.blink_repeat == 0) {
		set_led_brightness(led_state.final_brightness);
		led_state.mode = Mode::OFF;
	} else {
		set_led_brightness(led_state.next_brightness);
		uint32_t next = led_state.next_brightness;
		led_state.next_brightness = led_state.previous_brightness;
		led_state.previous_brightness = next;
		led_state.blink_repeat--;
		k_work_schedule(&led_update_work, led_state.blink_period);
	}

	k_mutex_unlock(&led_state.mutex);
}

void set_led_blink(uint32_t first_brightness, uint32_t second_brightness,
		   uint32_t repeat, k_timeout_t period)
{
	k_mutex_lock(&led_state.mutex, K_FOREVER);

	k_work_cancel_delayable(&led_update_work);

	led_state.next_brightness = first_brightness;
	led_state.previous_brightness = second_brightness;
	led_state.final_brightness = BRIGHTNESS_OFF;
	led_state.blink_repeat = repeat;
	led_state.blink_period = period;

	k_work_schedule(&led_update_work, K_NO_WAIT);

	k_mutex_unlock(&led_state.mutex);
}

void set_led_static(uint32_t brightness, k_timeout_t time)
{
	set_led_blink(brightness, BRIGHTNESS_OFF, 1, time);
}

int fp_led_init()
{
	if (!pwm_is_ready_dt(&cfg.pwms)) {
		LOG_ERR("PWM is not ready");
		return -ENODEV;
	}
	k_mutex_init(&led_state.mutex);
	k_work_init_delayable(&led_update_work, led_update_handler);
	return 0;
}

}

namespace fp_led
{

void enroll(void)
{
	k_mutex_lock(&led_state.mutex, K_FOREVER);
	if (led_state.mode == Mode::ENROLL) {
		k_mutex_unlock(&led_state.mutex);
		return;
	}
	led_state.mode = Mode::ENROLL;
	k_mutex_unlock(&led_state.mutex);

	set_led_static(BRIGHTNESS_ENROLL, DURATION_ENROLL);
	LOG_INF("Finger print led enroll.");
}

void match(void)
{
	k_mutex_lock(&led_state.mutex, K_FOREVER);
	if (led_state.mode == Mode::MATCH) {
		k_mutex_unlock(&led_state.mutex);
		return;
	}
	led_state.mode = Mode::MATCH;
	k_mutex_unlock(&led_state.mutex);

	set_led_static(BRIGHTNESS_MATCH, DURATION_MATCH);
	LOG_INF("Finger print led match.");
}

void match_ok(void)
{
	k_mutex_lock(&led_state.mutex, K_FOREVER);
	led_state.mode = Mode::MATCH_OK;
	k_mutex_unlock(&led_state.mutex);

	set_led_blink(BRIGHTNESS_MATCH_OK_LOW, BRIGHTNESS_MATCH_OK_HIGH, 10,
		      DURATION_MATCH_OK_PERIOD);
	LOG_INF("Finger print led match OK.");
}

void match_nok(void)
{
	k_mutex_lock(&led_state.mutex, K_FOREVER);
	led_state.mode = Mode::MATCH_NOK;
	k_mutex_unlock(&led_state.mutex);

	set_led_static(BRIGHTNESS_FAIL, DURATION_FAIL);
	LOG_INF("Finger print led match NOK.");
}

void off(void)
{
	k_mutex_lock(&led_state.mutex, K_FOREVER);
	if (led_state.mode == Mode::OFF || led_state.mode == Mode::MATCH_OK ||
	    led_state.mode == Mode::MATCH_NOK) {
		k_mutex_unlock(&led_state.mutex);
		return;
	}
	k_work_cancel_delayable(&led_update_work);
	set_led_brightness(BRIGHTNESS_OFF);
	LOG_INF("Finger print led off.");
	led_state.mode = Mode::OFF;
	k_mutex_unlock(&led_state.mutex);
}

} // namespace fp_led

SYS_INIT(fp_led_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
