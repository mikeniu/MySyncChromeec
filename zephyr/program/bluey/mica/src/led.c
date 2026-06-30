<<<<<<< HEAD   (a520918c7c89738378edf64bffe4ab612881c225 mica: Refactor 5V rail control logic)
||||||| BASE   (eeb1a820bd8d6e4770f061372227da0725998655 zephyr: fingerprint: egis630: Move PAL memory allocators fro)
/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mica lightbar LED control used to trigger a custom diagnostic sequence. */

#include "common/lightbar_policy_alt.h"
#include "lb_policy.h"
#include "led_lb_host_program.h"
#include "led_lightbar.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mica_led_diag, LOG_LEVEL_ERR);

/*
 * AP-triggered Diagnostics
 */
__override enum ec_status board_lightbar_custom_seq(uint8_t seq)
{
	switch (seq) {
	case LIGHTBAR_CMD_SEQ_RAMDUMP: /* Ramdump collection */
		lb_set_diag_policy(LED_ALT_POLICY_DIAG_RAMDUMP, 90000);
		return EC_RES_SUCCESS;

	case LIGHTBAR_CMD_SEQ_DIAG_CLEAR: /* Clear diagnostic state */
		lb_set_diag_policy(LED_ALT_POLICY_NORMAL, 0);
		return EC_RES_SUCCESS;

	case LIGHTBAR_CMD_SEQ_DIAG_LCD:
		/* Legacy behavior: LCD fail to diag 10 mins */
		lb_set_diag_policy(LED_ALT_POLICY_DIAG_LCD, 600000);
		LOG_ERR("LCD failed! LED Diag activated");
		return EC_RES_SUCCESS;

	default:
		return EC_RES_INVALID_PARAM;
	}
}
=======
/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mica lightbar LED control used to trigger a custom diagnostic sequence. */

#include "hooks.h"
#include "lb_policy.h"
#include "led_common.h"
#include "led_lb_host_program.h"
#include "led_lightbar.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mica_led_diag, LOG_LEVEL_ERR);

/* true while power failure is detected. */
static bool power_fail_flags;

/*
 * AP-triggered Diagnostics
 */
__override enum ec_status board_lightbar_custom_seq(uint8_t seq)
{
	switch (seq) {
	case LIGHTBAR_CMD_SEQ_RAMDUMP: /* Ramdump collection */
		lb_set_diag_policy(LED_ALT_POLICY_DIAG_RAMDUMP, 90000);
		return EC_RES_SUCCESS;

	case LIGHTBAR_CMD_SEQ_DIAG_CLEAR: /* Clear diagnostic state */
		lb_set_diag_policy(LED_ALT_POLICY_NORMAL, 0);
		return EC_RES_SUCCESS;

	case LIGHTBAR_CMD_SEQ_DIAG_LCD:
		/* Legacy behavior: LCD fail to diag 10 mins */
		lb_set_diag_policy(LED_ALT_POLICY_DIAG_LCD, 600000);
		LOG_ERR("LCD failed! LED Diag activated");
		return EC_RES_SUCCESS;

	default:
		return EC_RES_INVALID_PARAM;
	}
}

/*
 * EC-triggered Diagnostics
 */
void board_diag_led_power_fail(void)
{
	power_fail_flags = true;
	lb_set_diag_policy(LED_ALT_POLICY_DIAG_PWR, 600000);
	LOG_ERR("Power on failed! LED Diag activated");
}

/*
 * Clear the power fail diagnostic LED indication once AP
 * successfully reaches the startup state.
 *
 * This prevents the lightbar from remaining in a led diagnostic state
 * after AP has successfully completed the boot sequence.
 */
static void board_clear_diag_led(void)
{
	if (power_fail_flags == true) {
		power_fail_flags = false;
		lb_set_diag_policy(LED_ALT_POLICY_NORMAL, 0);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_clear_diag_led, HOOK_PRIO_DEFAULT);
>>>>>>> CHANGE (2f221e4ce8dfaa82e05b18c99219101302964b2a mica: Add a diagnostic lightbar pattern for power failure)
