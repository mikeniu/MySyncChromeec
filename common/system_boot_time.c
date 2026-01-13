/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "system.h"
#include "util.h"

#include <stdbool.h>

#ifdef CONFIG_SYSTEM_BOOT_TIME_LOGGING
/* Content of ap_boot_time will be lost on sysjump */
static struct ec_response_get_boot_time ap_boot_time;
#endif

#ifdef CONFIG_PLATFORM_EC_CONSOLE_CMD_BOOTTIME
#define UNINITIALIZED_TIME (-1ll)
static uint64_t first_S0_time = UNINITIALIZED_TIME; /* When did we enter S0? */
static uint64_t first_S5_time = UNINITIALIZED_TIME; /* When did we enter S5? */
#endif
/* This function updates timestamp for ap boot time params */
void update_ap_boot_time(enum boot_time_param param)
{
#ifdef CONFIG_SYSTEM_BOOT_TIME_LOGGING
	static bool ap_booted; /* tracks AP booted after #PLTRST */
	static bool pltrst_transition; /* tracks #PLTRST transtion */

	if (param > RESET_CNT) {
		ccprintf("invalid boot_time_param: %d\n", param);
		return;
	}
	if (param < RESET_CNT) {
		ap_boot_time.timestamp[param] = get_time().val;
		ccprintf("Boot Time: %d, %llu\n", param,
			 ap_boot_time.timestamp[param]);
	}

	/* warm reboot counter set to 1 only when #PLTRST transtion is complete
	 * and AP is already booted (ap_booted = 1).
	 * In initial boot or cold reboot scenario (ap_booted = 0) warm reboot
	 * counter will be set to 0.
	 */
	switch (param) {
#ifdef CONFIG_PLATFORM_EC_CONSOLE_CMD_BOOTTIME
	case RSMRST:
		if (first_S5_time == UNINITIALIZED_TIME) {
			first_S5_time = ap_boot_time.timestamp[RSMRST];
		}
		break;
#endif
	case PLTRST_HIGH:
		/* warm reboot happened */
		if (ap_booted && pltrst_transition)
			ap_boot_time.cnt = 1;
		else /* cold reboot happened */
			ap_boot_time.cnt = 0;

		if (pltrst_transition) {
			ap_booted = true;
			pltrst_transition = false;
		}
#ifdef CONFIG_PLATFORM_EC_CONSOLE_CMD_BOOTTIME
		if (first_S0_time == UNINITIALIZED_TIME) {
			first_S0_time = ap_boot_time.timestamp[PLTRST_HIGH];
		}
#endif
		break;
	case PLTRST_LOW:
		/* #PLTRST is in transition state */
		pltrst_transition = true;
		break;
	case RESET_CNT:
		ap_boot_time.cnt = 0;

		if (ap_booted)
			pltrst_transition = false;

		ap_booted = false;
		break;
	default:
		break;
	}
#endif
}

#ifdef CONFIG_SYSTEM_BOOT_TIME_LOGGING
/* Returns system boot time data */
static enum ec_status
host_command_get_boot_time(struct host_cmd_handler_args *args)
{
	struct ec_response_get_boot_time *boot_time = args->response;

	if (args->response_max < sizeof(*boot_time)) {
		return EC_RES_RESPONSE_TOO_BIG;
	}

	/* update current time */
	update_ap_boot_time(EC_CUR_TIME);

	/* copy data from ap_boot_time struct */
	memcpy(boot_time, &ap_boot_time, sizeof(*boot_time));

	args->response_size = sizeof(*boot_time);

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_GET_BOOT_TIME, host_command_get_boot_time,
		     EC_VER_MASK(0));

#ifdef CONFIG_PLATFORM_EC_CONSOLE_CMD_BOOTTIME
static int command_boot_time(int argc, const char **argv)
{
	const char *state_name;
	uint64_t display_time;

	if (argc != 2) {
		return EC_ERROR_PARAM_COUNT;
	}

	state_name = argv[1];
	if (strcasecmp(state_name, "S0") == 0) {
		display_time = first_S0_time;
	} else if (strcasecmp(state_name, "S5") == 0) {
		display_time = first_S5_time;
	} else {
		return EC_ERROR_PARAM1;
	}

	if (display_time == UNINITIALIZED_TIME) {
		ccprints("first %s: %lldms", state_name, -1ll);
	} else {
		ccprints("first %s: %lldms", state_name,
			 (int64_t)(display_time / MSEC));
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(boottime, command_boot_time, "<S0|S5>",
			"Expose boot time for firmware.BootTime.");
#endif /* CONFIG_PLATFORM_EC_CONSOLE_CMD_BOOTTIME */
#endif /* CONFIG_SYSTEM_BOOT_TIME_LOGGING */
