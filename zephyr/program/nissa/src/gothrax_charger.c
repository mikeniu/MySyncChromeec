/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "usb_pd.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(charger, LOG_LEVEL_INF);

#define MAX_POWER_65W 20000
#define DEFAULT_POWER 15000
#define IS_SKU_ID_IN_RANGE(sku_id) \
	((sku_id >= 0x2A0000) && (sku_id <= 0x2A0010))

static uint32_t calculate_max_voltage(uint32_t val, uint32_t sku_id)
{
	if (val == MAX20 || IS_SKU_ID_IN_RANGE(sku_id)) {
		return MAX_POWER_65W;
	} else {
		return DEFAULT_POWER;
	}
}

static void adapter_voltage_limit(void)
{
	int ret;
	uint32_t val;
	int port;
	uint32_t sku_id;
	uint32_t max_voltage;

	ret = cros_cbi_get_fw_config(ADAPTER_VOLTAGE_LIMIT, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d",
			ADAPTER_VOLTAGE_LIMIT);
	} else {
		ret = cbi_get_sku_id(&sku_id);
		if (ret != 0) {
			LOG_ERR("Error retrieving CBI SKU_ID field");
		}
	}

	if (ret != 0) {
		max_voltage = DEFAULT_POWER;
	} else {
		max_voltage = calculate_max_voltage(val, sku_id);
	}

	for (port = 0; port < board_get_usb_pd_port_count(); port++) {
		pd_set_external_voltage_limit(port, max_voltage);
	}
}

DECLARE_HOOK(HOOK_INIT, adapter_voltage_limit, HOOK_PRIO_DEFAULT - 1);

#if defined(CONFIG_PLATFORM_EC_BATTERY_HW_PRESENT_CUSTOM) || \
	defined(CONFIG_TEST_NISSA_EPIC)
enum battery_present battery_hw_present(void)
{
	const struct gpio_dt_spec *batt_pres;

	batt_pres = GPIO_DT_FROM_NODELABEL(gpio_ec_battery_pres_odl);

	/* The GPIO is low when the battery is physically present */
	return gpio_pin_get_dt(batt_pres) ? BP_NO : BP_YES;
}
#endif
