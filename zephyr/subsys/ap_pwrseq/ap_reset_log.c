/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "ap_reset_log.h"
#include "ec_commands.h"

static void reset_handler(struct ap_power_ev_callback *callback,
			  struct ap_power_ev_data data)
{
	report_ap_reset(CHIPSET_RESET_AP_REQ);
}
AP_POWER_EVENT_CALLBACK_DEFINE(reset_handler, AP_POWER_RESET);
