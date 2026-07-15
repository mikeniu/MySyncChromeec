/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c.h"
#include "i2c/i2c.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/devicetree.h>

#define CHECK_PDC_ADDR_MATCH(node_id, target_addr) \
	(DT_REG_ADDR(node_id) == (target_addr)) ||

#define IS_PDC_ADDRESS(addr)                                             \
	(DT_FOREACH_STATUS_OKAY_VARGS(realtek_rts54_pdc,                 \
				      CHECK_PDC_ADDR_MATCH, addr)        \
	 DT_FOREACH_STATUS_OKAY_VARGS(ti_tps6699_pdc,                    \
				      CHECK_PDC_ADDR_MATCH, addr) false)

#define CHECK_PDC_PORT_MATCH(node_id, target_port) \
	(I2C_PORT_BY_DEV(node_id) == (target_port)) ||

#define IS_PDC_PORT(port)                                                \
	(DT_FOREACH_STATUS_OKAY_VARGS(realtek_rts54_pdc,                 \
				      CHECK_PDC_PORT_MATCH, port)        \
	 DT_FOREACH_STATUS_OKAY_VARGS(ti_tps6699_pdc,                    \
				      CHECK_PDC_PORT_MATCH, port) false)

int tanjiro_pdc_passthru_allowed(const struct i2c_port_t *port,
				 uint16_t addr_flags)
{
	uint16_t addr = addr_flags & EC_I2C_ADDR_MASK;

	/* Dynamically check if slave address belongs to any PDC node in Devicetree */
	if (!IS_PDC_ADDRESS(addr))
		return 0;

	/* Allow PDC reads unconditionally, but restrict PDC writes to when PDC is suspended */
	if (!(addr_flags & EC_I2C_FLAG_READ)) {
		if (!pdc_power_mgmt_is_suspended())
			return 0;
	}

	return 1;
}

__overridable int
board_allow_i2c_passthru(const struct i2c_cmd_desc_t *cmd_desc)
{
	return cmd_desc->port == I2C_PORT_BATTERY || IS_PDC_PORT(cmd_desc->port);
}

