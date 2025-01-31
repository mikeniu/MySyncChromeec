/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Brox USB-C board functions
 */

#include "cros_board_info.h"
#include "ec_commands.h"
#include "host_command.h"
#include "usb_pd.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(brox_usbc, LOG_LEVEL_INF);

enum ec_pd_port_location board_get_pd_port_location(int port)
{
	switch (port) {
	case 0:
		return EC_PD_PORT_LOCATION_LEFT_FRONT;
	case 1:
		return EC_PD_PORT_LOCATION_LEFT_BACK;
	}
	return EC_PD_PORT_LOCATION_UNKNOWN;
}

__override const struct device *board_get_pdc_for_port(uint8_t port_num)
{
	int rv;
	uint32_t sku_id;
	const struct device *chosen = NULL;

	rv = cbi_get_sku_id(&sku_id);
	if (rv) {
		LOG_ERR("%s: Cannot read CBI SKU ID: %d. PDC config unknown!",
			__func__, rv);
		return NULL;
	}

	if (port_num >= CONFIG_USB_PD_PORT_MAX_COUNT) {
		LOG_ERR("%s: Invalid port number (%u)", __func__, port_num);
		return NULL;
	}

	switch (sku_id) {
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x21:
	case 0x22:
	case 0x23:
		/* RTK PDC */
		switch (port_num) {
		case 0:
			chosen = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0_rtk));
			break;
		case 1:
			chosen = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1_rtk));
			break;
		}
		break;
	case 0x24:
		/* TI PDC */
		switch (port_num) {
		case 0:
			chosen = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0_ti));
			break;
		case 1:
			chosen = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1_ti));
			break;
		}
		break;
	}

	return chosen;
}
