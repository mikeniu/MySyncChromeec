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
