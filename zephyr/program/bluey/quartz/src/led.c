/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Quartz lightbar LED control used to trigger a custom diagnostic sequence. */

#include "common/lightbar_policy_alt.h"
#include "lb_policy.h"
#include "led_lb_host_program.h"
#include "led_lightbar.h"

#include <zephyr/logging/log.h>

/*
 * AP-triggered Diagnostics
 */
__override enum ec_status board_lightbar_custom_seq(uint8_t seq)
{
	if (seq == LIGHTBAR_CMD_SEQ_RAMDUMP) {
		lb_set_diag_policy(LED_ALT_POLICY_DIAG_RAMDUMP, 90000);
		return EC_RES_SUCCESS;
	} else {
		return EC_RES_INVALID_PARAM;
	}
}
