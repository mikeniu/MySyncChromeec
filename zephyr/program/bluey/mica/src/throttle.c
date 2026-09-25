/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "chipset.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "throttle_ap.h"

/* True if the system is booting with AC power only (no battery present). */
static bool ac_only_boot;

void chipset_throttle_cpu(int throttle)
{
	/* Do not allow changing throttle state during AC-only boot to ensure
	 * PROCHOT remains asserted to limit SoC power consumption.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ON) || ac_only_boot)
		return;

	if (throttle) {
		gpio_set_level(
			GPIO_CPU_PROCHOT,
			!IS_ENABLED(
				CONFIG_PLATFORM_EC_POWERSEQ_CPU_PROCHOT_ACTIVE_LOW));
	} else {
		gpio_set_level(
			GPIO_CPU_PROCHOT,
			IS_ENABLED(
				CONFIG_PLATFORM_EC_POWERSEQ_CPU_PROCHOT_ACTIVE_LOW));
	}
}

static void throttle_init(void)
{
	uint32_t board_id;
	int rv;

	rv = cbi_get_board_version(&board_id);
	if (rv != EC_SUCCESS) {
		board_id = 0;
	}

	if (board_id <= 1)
		gpio_set_flags(GPIO_CPU_PROCHOT, GPIO_OUTPUT | GPIO_OPEN_DRAIN |
							 GPIO_OUTPUT_INIT_LOW);
}
DECLARE_HOOK(HOOK_INIT, throttle_init, HOOK_PRIO_DEFAULT);

/*
 * Limit SoC power consumption during AC-only boot (battery not present) by
 * asserting system throttling (PROCHOT). This prevents the device from
 * consuming more than 65 W, avoiding abnormal system shutdowns.
 */
static void board_ac_only_boot(void)
{
	ac_only_boot = 0;

	if (battery_is_present() != BP_YES) {
		gpio_pin_set_dt(
			GPIO_DT_FROM_NODELABEL(gpio_ec_sys_throttle_mira), 1);
		ac_only_boot = 1;
	}
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_ac_only_boot, HOOK_PRIO_DEFAULT);
