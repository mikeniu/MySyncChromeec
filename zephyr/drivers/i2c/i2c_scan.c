/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

static enum ec_status hc_ish_i2c_scan(struct host_cmd_handler_args *args)
{
	uint8_t cnt = 0, first = 0x04, last = 0x77;
	const struct device *i2c_dev = DEVICE_DT_GET(DT_CHOSEN(hc_i2c));
	uint16_t *addr_row;

	addr_row = (uint16_t *)args->response;
	for (uint8_t i = 0; i <= last; i += ISH_I2C_SCAN_COLS) {
		*addr_row = 0;
		for (uint8_t j = 0; j < ISH_I2C_SCAN_COLS; j++) {
			if (i + j < first || i + j > last) {
				printk("   ");
				continue;
			}

			struct i2c_msg msgs[1];
			uint8_t dst;

			/* Send the address to read from */
			msgs[0].buf = &dst;
			msgs[0].len = 1U;
			msgs[0].flags = I2C_MSG_READ | I2C_MSG_STOP;
			if (i2c_transfer(i2c_dev, &msgs[0], 1, i + j) == 0) {
				*addr_row |= 1 << j;
				++cnt;
			}
		}
		addr_row++;
	}

	args->response_size = sizeof(struct ec_response_ish_i2c_scan);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_ISH_I2C_SCAN, hc_ish_i2c_scan, EC_VER_MASK(0));
