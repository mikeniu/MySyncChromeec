/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "lb_policy.h"
#include "led_lightbar.h"

#define LIGHTBAR_CMD_SEQ_DIAG_CLEAR 100
#define LIGHTBAR_CMD_SEQ_DIAG_LED 101
#define LIGHTBAR_CMD_SEQ_DIAG_PWR 102

/*
 * AP-triggered Diagnostics
 */
__override enum ec_status board_lightbar_custom_seq(uint8_t seq)
{
	switch (seq) {
	case LIGHTBAR_CMD_SEQ_DIAG_CLEAR: /* Clear diagnostic state */
		lb_set_diag_policy(LED_ALT_POLICY_NORMAL, 0);
		return EC_RES_SUCCESS;

	case LIGHTBAR_CMD_SEQ_DIAG_LED:
		lb_set_diag_policy(LED_ALT_POLICY_DIAG_LCD, 0);
		return EC_RES_SUCCESS;

	case LIGHTBAR_CMD_SEQ_DIAG_PWR:
		lb_set_diag_policy(LED_ALT_POLICY_DIAG_PWR, 0);
		return EC_RES_SUCCESS;

	default:
		return EC_RES_INVALID_PARAM;
	}
}

/*
 * EC-triggered Diagnostics
 */
void power_fail_led_control(void)
{
	lb_set_diag_policy(LED_ALT_POLICY_DIAG_PWR, 0);
}
