/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "fan.h"
#include "gpio/gpio.h"
#include "hooks.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

/*
 * Pujjo fan support
 */
test_export_static void fan_init(void)
{
	uint32_t fan_config;
	/*
	 * Retrieve the fan config.
	 */
	if (cros_cbi_get_fw_config(FW_FAN, &fan_config) != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_FAN);
		return;
	}

	if (fan_config != FW_FAN_PRESENT) {
		uint32_t board_version, sku_id;

		if (cbi_get_board_version(&board_version) != 0) {
			LOG_ERR("Error retrieving CBI BOARD_VERSION field");
			return;
		}
		if (cbi_get_sku_id(&sku_id) != 0) {
			LOG_ERR("Error retrieving CBI SKU_ID field");
			return;
		}
		bool sku_id_is_wrong =
				/* a0012 through a0016 but not a0014 */
				(sku_id >= 0xa0012 && sku_id <= 0xa0016 && sku_id != 0xa0014)
				/* a002a through a002e but not a002c */
				|| (sku_id >= 0xa002a && sku_id <= 0xa002e && sku_id != 0xa002c);

		if (board_version == 3 && sku_id_is_wrong) {
			/* Always enable the fan for device with the wrong SKU ID. */
			gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_fan_enable),
								GPIO_OUTPUT);
		}
	} else {
		/* Configure the fan enable GPIO */
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_fan_enable),
				      GPIO_OUTPUT);
	}
}
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_POST_FIRST);
