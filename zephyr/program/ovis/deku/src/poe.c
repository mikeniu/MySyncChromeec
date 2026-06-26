/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power Over Ethernet support */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"

/**
 * Activate/Deactivate the POE GPIO pin considering active high or low.
 */
void enable_poe(int enabled)
{
	gpio_set_level(GPIO_POE_CS_OUT_L, !enabled);
}

/**
 * Initialize Power Over Ethernet module.
 */
static void poe_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, poe_init, HOOK_PRIO_DEFAULT);

/**
 * Host command to toggle Power over Ethernet.
 *
 */
static enum ec_host_cmd_status
switch_command_enable_poe(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_switch_enable_poe *p = args->input_buf;

	enable_poe(p->enabled);

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_SWITCH_ENABLE_POE,
			     switch_command_enable_poe, EC_VER_MASK(0),
			     struct ec_params_switch_enable_poe);
