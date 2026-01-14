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
	struct gpio_dt_spec btn_ign_gpio;
};

const btn_ign_config cfg = {
	.btn_ign_gpio = GPIO_DT_SPEC_GET(DT_CHOSEN(cros_fp_btn_ign_out), gpios),
};

k_work_delayable delayed_deactivation;

void delayed_deactivation_handler(struct k_work *)
{
	gpio_pin_set_dt(&cfg.btn_ign_gpio, 0);
	LOG_INF("Power button ignore deactivated.");
}

int btn_ign_init()
{
	auto ret =
		gpio_pin_configure_dt(&cfg.btn_ign_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("Pin configuration failed: %d", ret);
		return ret;
	}
	k_work_init_delayable(&delayed_deactivation,
			      delayed_deactivation_handler);
	return 0;
}

}

namespace btn_ign_out
{

void activate()
{
	k_work_cancel_delayable(&delayed_deactivation);
	gpio_pin_set_dt(&cfg.btn_ign_gpio, 1);
	LOG_INF("Power button ignore activated.");
}

void deactivate()
{
	k_work_schedule(&delayed_deactivation, K_SECONDS(2));
}

} // namespace btn_ign_out

SYS_INIT(btn_ign_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
