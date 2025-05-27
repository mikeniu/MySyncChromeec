/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"
#include "cros_board_info.h"
#include "include/system.h"
#include "usbc/pdc_runtime_port_config.h"

#include <stdint.h>

#include <zephyr/logging/log.h>

int board_get_pdc_for_port_rvp(int port, const struct device **dev)
{
	switch (port) {
	case 0:
		*dev = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(pd_pow_detect)) ?
			       DEVICE_DT_GET(DT_NODELABEL(pd_pow_port0)) :
			       NULL;
		return 0;
	case 1:
		*dev = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(pd_pow_detect)) ?
			       DEVICE_DT_GET(DT_NODELABEL(pd_pow_port1)) :
			       NULL;
		return 0;
	case 2:
		if (system_get_board_version() == PTL_RVP_BOARD_ID) {
			*dev = gpio_pin_get_dt(
				       GPIO_DT_FROM_NODELABEL(pd_pow_p2_detect)) ?
				       DEVICE_DT_GET(DT_NODELABEL(pd_pow_port2)) :
				       NULL;
		}
		return 0;
	default:
	}

	return -ENOENT;
}

int board_get_pdc_for_port_gcs(int port, const struct device **dev)
{
	switch (port) {
	case 0:
		*dev = DEVICE_DT_GET(DT_NODELABEL(pd_pow_port0));
		return 0;
	case 1:
		*dev = DEVICE_DT_GET(DT_NODELABEL(pd_pow_port1));
		return 0;
	default:
	}

	return -ENOENT;
}

/** Supply pdc_power_mgmt with dynamic USB-C port configuration data */
int board_get_pdc_for_port(int port, const struct device **dev)
{
	static int board_id = -1;
	if (dev == NULL) {
		return -EINVAL;
	}

	if (board_id == -1) {
		board_id = system_get_board_version();
	}

	if (board_id == -1) {
		return -EINVAL;
	}

	switch(board_id) {
	case PTL_RVP_BOARD_ID:
		return board_get_pdc_for_port_rvp(port, dev);

	case PTL_GCS_BOARD_ID:
		return board_get_pdc_for_port_gcs(port, dev);

	default:
	}

	return -ENOENT;
}
