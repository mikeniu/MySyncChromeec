/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB-C board functions for the Trulo reference board only
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "usbc/pdc_runtime_port_config.h"

#include <stdint.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pujjocento_usbc, LOG_LEVEL_INF);

/** Supply pdc_power_mgmt with dynamic USB-C port configuration data */
int board_get_pdc_for_port(int port, const struct device **dev)
{
	int rv;
	uint32_t usb_pdc;

	if (dev == NULL) {
		LOG_ERR("%s: Bad pointer", __func__);
		return -EINVAL;
	}

	rv = cros_cbi_get_fw_config(FW_PDC_CONTROL, &usb_pdc);
	if (rv) {
		LOG_ERR("%s: Cannot read CBI SKU ID: %d. PDC config unknown!",
			__func__, rv);

		*dev = NULL;
		return -EIO;
	}

	switch (usb_pdc) {
	case FW_PDC_CONTROL_RTK_BYPASS:
		/* RTK PDC */
		switch (port) {
		case 0:
			*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0));
			return 0;
		case 1:
			*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1));
			return 0;
		}
		break;
	case FW_PDC_CONTROL_TI_BYPASS:
		/* TI PDC */
		switch (port) {
		case 0:
			*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0_ti));
			return 0;
		case 1:
			*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1_ti));
			return 0;
		}
		break;
	}

	LOG_ERR("%s: No entry for SKU (0x%x) or port %d", __func__, usb_pdc,
		port);

	*dev = NULL;
	return -ENOENT;
}
