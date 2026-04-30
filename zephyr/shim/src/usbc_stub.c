/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This file provides a stub implementation for the power delivery
 * sub-system when a USB-C stack (TCPMv2 nor PD Controller) is not
 * available.
 */

#include "charge_manager.h"
#include "ec_commands.h"

/* Test code may still override the board specific routines. */
__overridable uint8_t board_get_usb_pd_port_count(void)
{
	return 0;
}

__overridable int board_set_active_charge_port(int charge_port)
{
	return EC_SUCCESS;
}

enum pd_power_role pd_get_power_role(int port)
{
	/* No PD stack present, set role to source to prevent the charge
	 * manager from charging from the port.
	 */
	return PD_ROLE_SOURCE;
}

void pd_request_power_swap(int port)
{
}

void pd_set_new_power_request(int port)
{
}

void tc_set_msg_header_data_role(int port, enum pd_data_role data_role)
{
}
