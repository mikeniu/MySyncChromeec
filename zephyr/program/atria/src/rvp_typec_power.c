/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c.h"

#include <usbc/pdc_power_mgmt.h>

#define JAMES_TOWN_CHARGER_I2C_ADDR 0x4a
#define JAMES_TOWN_CONTROL0_REG 0x39
#define JAMES_TOWN_BYPASS_MODE 0x0003
#define JAMES_TOWN_DISCONNECT_MODE 0x0000

static void atria_attached(int port)
{
	i2c_write16(I2C_PORT_CHARGER, JAMES_TOWN_CHARGER_I2C_ADDR,
		    JAMES_TOWN_CONTROL0_REG, JAMES_TOWN_BYPASS_MODE);
}

static void atria_detached(int port)
{
	i2c_write16(I2C_PORT_CHARGER, JAMES_TOWN_CHARGER_I2C_ADDR,
		    JAMES_TOWN_CONTROL0_REG, JAMES_TOWN_DISCONNECT_MODE);
}

static int atria_pdc_cb_init(void)
{
	pdc_power_mgmt_register_board_callback(PDC_BOARD_CB_ATTACHED,
					       atria_attached);
	pdc_power_mgmt_register_board_callback(PDC_BOARD_CB_UNATTACH,
					       atria_detached);
	return 0;
}
SYS_INIT(atria_pdc_cb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
