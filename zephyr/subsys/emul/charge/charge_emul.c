/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_state.h"
#include "console.h"
#include "util.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(charge_emul, CONFIG_PLATFORM_EC_CHARGE_EMUL_LOG_LEVEL);

static enum led_pwr_state emul_led_pwr_state = LED_PWRS_IDLE;
static int emul_active_port = CHARGE_PORT_NONE;

enum led_pwr_state led_pwr_get_state(void)
{
	return emul_led_pwr_state;
}

int charge_manager_get_active_charge_port(void)
{
	return emul_active_port;
}

void emul_charge_set_led_pwr_state(enum led_pwr_state state)
{
	emul_led_pwr_state = state;
}

void emul_charge_set_active_port(int port)
{
	emul_active_port = port;
}

#ifdef CONFIG_PLATFORM_EC_CHARGE_EMUL_CONSOLE_COMMAND
static int command_emul_charge(int argc, const char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "state")) {
		if (argc < 3)
			return EC_ERROR_PARAM_COUNT;
		emul_led_pwr_state = strtol(argv[2], NULL, 0);
		LOG_INF("Set emul_led_pwr_state to %d", emul_led_pwr_state);
	} else if (!strcasecmp(argv[1], "port")) {
		if (argc < 3)
			return EC_ERROR_PARAM_COUNT;
		emul_active_port = strtol(argv[2], NULL, 0);
		LOG_INF("Set emul_active_port to %d", emul_active_port);
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(emul_charge, command_emul_charge, "state|port <val>",
			"Set emulator charge values");
#endif
