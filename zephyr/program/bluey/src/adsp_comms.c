/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adsp_comms.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "ec_commands.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bluey_adsp, LOG_LEVEL_INF);

static int active_charge_port = CHARGE_PORT_NONE;
static enum led_pwr_state active_charge_state = LED_PWRS_IDLE;

int charge_manager_get_active_charge_port(void)
{
	return active_charge_port;
}

enum led_pwr_state led_pwr_get_state(void)
{
	return active_charge_state;
}

static void adsp_power_state_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	LOG_INF("ADSP: Power State: 0x%04x", data);
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_DEFAULT, ADSP_POWER_STATE_REG_VAL,
		       adsp_power_state_cb);

static void adsp_oem_magic_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	if (data == 0xec) {
		LOG_INF("ADSP: Comms established");
	} else {
		LOG_INF("ADSP: Incorrect magic packet received (0x%04x)", data);
	}
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_MAGIC,
		       adsp_oem_magic_cb);

static void adsp_oem_version_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	if (data == 1) {
		LOG_INF("ADSP: Version 1 identified");
	} else {
		LOG_ERR("ADSP: Incorrect version received: %d (expected 1)",
			data);
	}
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_VERSION,
		       adsp_oem_version_cb);

static void adsp_oem_charge_port_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	if (data == 0) {
		active_charge_port = CHARGE_PORT_NONE;
		LOG_INF("ADSP: Charging disabled");
	} else if (data >= 1 && data <= 3) {
		active_charge_port = data - 1;
		LOG_INF("ADSP: Charging from USB%d", active_charge_port);
	} else {
		LOG_ERR("ADSP: Invalid charge port: %d", data);
	}
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_OEM_CUSTOM, ADSP_OEM_CUSTOM_REG_CHARGE_PORT,
		       adsp_oem_charge_port_cb);

static void adsp_oem_charge_state_cb(uint8_t fid, uint8_t addr, uint16_t data)
{
	switch (data) {
	case 0:
		active_charge_state = LED_PWRS_CHARGE;
		LOG_INF("ADSP: State - Charge");
		break;
	case 1:
		active_charge_state = LED_PWRS_DISCHARGE;
		LOG_INF("ADSP: State - Discharge");
		break;
	case 2:
		active_charge_state = LED_PWRS_ERROR;
		LOG_INF("ADSP: State - Error");
		break;
	case 3:
		active_charge_state = LED_PWRS_IDLE;
		LOG_INF("ADSP: State - Idle");
		break;
	case 4:
		active_charge_state = LED_PWRS_FORCED_IDLE;
		LOG_INF("ADSP: State - Forced Idle");
		break;
	case 5:
		active_charge_state = LED_PWRS_CHARGE_NEAR_FULL;
		LOG_INF("ADSP: State - Charge Near Full");
		break;
	default:
		LOG_ERR("ADSP: Invalid charge state: %d", data);
		return;
	}
}
ADSP_COMMS_REGISTER_CB(ADSP_FEATURE_OEM_CUSTOM,
		       ADSP_OEM_CUSTOM_REG_CHARGE_STATE,
		       adsp_oem_charge_state_cb);
