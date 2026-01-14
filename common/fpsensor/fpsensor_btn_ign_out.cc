/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor_btn_ign_out.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>

LOG_MODULE_REGISTER(btn_ign, LOG_LEVEL_INF);

namespace
{
struct btn_ign_config {
	const struct gpio_dt_spec *btn_ign_gpio;
};

const btn_ign_config cfg = {
	.btn_ign_gpio = GPIO_DT_FROM_ALIAS(gpio_btn_ign_out),
};

void delayed_deactivation_handler(struct k_work *)
{
	gpio_pin_set_dt(cfg.btn_ign_gpio, 0);
	LOG_INF("Power button ignore deactivated.");
}

K_WORK_DELAYABLE_DEFINE(delayed_deactivation, delayed_deactivation_handler);

bool is_active = false;

void activate()
{
	if (is_active) {
		return;
	}
	k_work_cancel_delayable(&delayed_deactivation);
	gpio_pin_set_dt(cfg.btn_ign_gpio, 1);
	is_active = true;
	LOG_INF("Power button ignore activated.");
}

void deactivate()
{
	if (!is_active) {
		return;
	}
	k_work_schedule(
		&delayed_deactivation,
		K_MSEC(CONFIG_PLATFORM_EC_FINGERPRINT_BTN_IGN_OUT_DELAY_MS));
	is_active = false;
}

} // namespace

namespace btn_ign_out
{

void fp_update_sensor_mode(std::uint32_t sensor_mode)
{
	if (sensor_mode &
	    (FP_MODE_ENROLL_SESSION | FP_MODE_MATCH | FP_MODE_CAPTURE))
		activate();
	else
		deactivate();
}

} // namespace btn_ign_out
