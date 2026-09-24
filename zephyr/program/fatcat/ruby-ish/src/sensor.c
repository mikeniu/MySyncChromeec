/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio/gpio_int.h"
#include "hooks.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ruby_ish_sensor, LOG_LEVEL_INF);

/*
 * motionsense.dtsi config ALS only active in S0.
 * When the AP suspends, ALS is inactive so interrupt is disabled;
 * enable interrupt when AP resumes.
 */
static void ruby_ish_als_interrupt_suspend(void)
{
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_als_rgb));
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, ruby_ish_als_interrupt_suspend,
	     HOOK_PRIO_DEFAULT);

static void ruby_ish_als_interrupt_resume(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_als_rgb));
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, ruby_ish_als_interrupt_resume,
	     HOOK_PRIO_DEFAULT);
