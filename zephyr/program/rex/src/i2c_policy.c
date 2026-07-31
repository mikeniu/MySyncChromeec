/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "i2c.h"
#include "stdbool.h"
#include "system.h"

#include <zephyr/devicetree.h>

int rex_tcpc_ps8815_allow_passthru(int port,
				   const struct ec_params_i2c_passthru *params,
				   bool protect_by_ap)
{
#if DT_NODE_EXISTS(DT_NODELABEL(tcpc_ps8815_port1))
	uint32_t usb_db_type;

	if (protect_by_ap)
		return false;

	if (!system_is_locked())
		return true;

	return (port == I2C_PORT_BY_DEV(DT_NODELABEL(tcpc_ps8815_port1)) &&
		cros_cbi_get_fw_config(FW_USB_DB, &usb_db_type) == EC_SUCCESS &&
		usb_db_type == FW_USB_DB_USB3);
#else
	return false;
#endif
}

int rex_usb_c1_anx7452_retimer_allow_passthru(
	int port, const struct ec_params_i2c_passthru *params,
	bool protect_by_ap)
{
#if DT_NODE_EXISTS(DT_NODELABEL(usb_c1_anx7452_retimer)) && \
	defined(FW_USB_DB_USB4_ANX7452_V2)
	uint32_t usb_db_type;

	if (protect_by_ap)
		return false;

	if (!system_is_locked())
		return true;

	return (port == I2C_PORT_BY_DEV(DT_NODELABEL(usb_c1_anx7452_retimer)) &&
		cros_cbi_get_fw_config(FW_USB_DB, &usb_db_type) == EC_SUCCESS &&
		usb_db_type == FW_USB_DB_USB4_ANX7452_V2);
#else
	return false;
#endif
}
