/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c.h"

#include <usbc/pdc_power_mgmt.h>

static void atria_attached_snk(int port)
{
	i2c_write16(I2C_PORT_CHARGER, 0x4a, 0x39, 0x0003);
}

static void atria_attached_src(int port)
{
	i2c_write16(I2C_PORT_CHARGER, 0x4a, 0x39, 0x0007);
}

static int atria_pdc_cb_init(void)
{
	pdc_power_mgmt_register_board_callback(PDC_BOARD_CB_ATTACHED_SNK,
					       atria_attached_snk);
	pdc_power_mgmt_register_board_callback(PDC_BOARD_CB_ATTACHED_SRC,
					       atria_attached_src);
	return 0;
}
SYS_INIT(atria_pdc_cb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
