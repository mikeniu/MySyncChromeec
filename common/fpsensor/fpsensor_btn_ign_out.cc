/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor_btn_ign_out.h"
#include "fpsensor/fpsensor_platform.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>

#include <chrono>

#pragma GCC poison global_context

LOG_MODULE_REGISTER(fp_btn_ign_out, LOG_LEVEL_INF);

namespace fp_btn_ign_out
{

namespace
{

	const gpio_dt_spec kBtnIgnGpio =
		GPIO_DT_SPEC_GET(DT_ALIAS(gpio_btn_ign_out), gpios);

	FpButtonIgnoreController controller;

	int init_btn_ign_out()
	{
		return controller.init();
	}
	SYS_INIT(init_btn_ign_out, POST_KERNEL,
		 CONFIG_APPLICATION_INIT_PRIORITY);

} /* namespace */

int FpButtonIgnoreController::init()
{
	/* Configure and initialize in case zephyr didn't do it */
	int ret = gpio_pin_configure_dt(&kBtnIgnGpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure power button ignore GPIO: %d",
			ret);
	}
	return ret;
}

#ifdef CONFIG_CROS_EC_RW
namespace
{

	constexpr std::chrono::milliseconds kDeactivationDelay(
		CONFIG_PLATFORM_EC_FINGERPRINT_BTN_IGN_OUT_DELAY_MS);

	K_SEM_DEFINE(btn_ign_lock, 1, 1);

	void deactivation_handler(k_work *)
	{
		controller.on_deactivation_timeout();
	}

	K_WORK_DELAYABLE_DEFINE(deactivation_dwork, deactivation_handler);

} /* namespace */

void FpButtonIgnoreController::on_deactivation_timeout()
{
	fp_platform::ScopedSemLock lock(btn_ign_lock);
	if (!is_active_) {
		gpio_pin_set_dt(&kBtnIgnGpio, 0);
		LOG_INF("Power button ignore deactivated.");
	}
}

void FpButtonIgnoreController::activate()
{
	fp_platform::ScopedSemLock lock(btn_ign_lock);
	if (!is_active_) {
		is_active_ = true;
		k_work_cancel_delayable(&deactivation_dwork);
		gpio_pin_set_dt(&kBtnIgnGpio, 1);
		LOG_INF("Power button ignore activated.");
	}
}

void FpButtonIgnoreController::deactivate()
{
	fp_platform::ScopedSemLock lock(btn_ign_lock);
	if (is_active_) {
		is_active_ = false;
		k_work_schedule(&deactivation_dwork,
				K_MSEC(kDeactivationDelay.count()));
	}
}

void FpButtonIgnoreController::update(FpSensorMode sensor_mode)
{
	if (sensor_mode.is_enroll_session() || sensor_mode.is_match() ||
	    sensor_mode.is_capture()) {
		activate();
	} else {
		deactivate();
	}
}

void update(FpSensorMode sensor_mode)
{
	controller.update(sensor_mode);
}

#endif /* CONFIG_CROS_EC_RW */

} /* namespace fp_btn_ign_out */
