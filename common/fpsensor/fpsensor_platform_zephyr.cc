/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_platform.h"
#include "task.h"

#include <zephyr/pm/policy.h>

namespace fp_platform
{

void lock_pm()
{
	pm_policy_state_all_lock_get();
}

void unlock_pm()
{
	pm_policy_state_all_lock_put();
}

void enable_sensor_irq()
{
}

void enter_sensor_low_power(uint32_t)
{
	fp_sensor_low_power();
}

void prepare_for_mode_change()
{
	/*
	 * We are about to change sensor mode, so exit any
	 * previous states.
	 */
	fp_idle();
}

void prepare_for_sensor_or_timer_event(uint32_t evt)
{
	/* On timeout, put sensor into idle state. */
	if (evt & TASK_EVENT_TIMER) {
		fp_idle();
	}
}

void flush_spi_after_acquire()
{
}

} /* namespace fp_platform */
