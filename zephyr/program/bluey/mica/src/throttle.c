/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "throttle_ap.h"

void chipset_throttle_cpu(int throttle)
{
	if (!chipset_in_state(CHIPSET_STATE_ON))
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
