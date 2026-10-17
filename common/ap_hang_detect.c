/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* AP hang detect logic */

#include "ap_reset_log.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "power_button.h"
#include "timer.h"
#include "util.h"

#include <stdbool.h>

/* Console output macro */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, "APHD: " format, ##args)

/*
 * Delay between forcing AP shutdown and polling for G3
 * (CHIPSET_STATE_HARD_OFF), and maximum number of 1s polls to wait for G3 (some
 * chipsets such as Skylake hold PWRBTN_L for 4s to reach S5 and then wait
 * s5_inactivity_timeout = 10s before entering G3).
 */
#define HANG_DETECT_COLD_REBOOT_OFF_DELAY_US SECOND
#define HANG_DETECT_COLD_REBOOT_MAX_RETRIES 20

static uint16_t reboot_timeout_sec;
static uint8_t bootstatus = EC_HANG_DETECT_AP_BOOT_NORMAL;
static uint8_t power_on_retries;
static bool cold_reset_pending;

static void hang_detect_power_on(void);
DECLARE_DEFERRED(hang_detect_power_on);

static void hang_detect_power_on(void)
{
	if (cold_reset_pending) {
		if (chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
			cold_reset_pending = false;
			power_on_retries = HANG_DETECT_COLD_REBOOT_MAX_RETRIES;
			CPRINTS("Powering on AP after cold reset");
			/*
			 * Re-record CHIPSET_RESET_HANG_REBOOT after G3 is
			 * reached because S0 -> S5 -> G3 forced shutdown
			 * transitions overwrite the AP reset log with POWERFAIL
			 * / G3.
			 */
			report_ap_reset(CHIPSET_RESET_HANG_REBOOT);
			chipset_power_on();
		} else if (power_on_retries == 0) {
			cold_reset_pending = false;
			CPRINTS("Failed to reach G3 for cold reboot");
			return;
		} else {
			power_on_retries--;
		}
	} else {
		if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			power_on_retries = 0;
			return;
		}

		if (power_on_retries == 0) {
			CPRINTS("Failed to power on AP after cold reboot");
			return;
		}

		power_on_retries--;

		/*
		 * Pulse power button only in settled S5 or G3; during G3S5/S5G3
		 * transitions chipset_in_state(ANY_OFF) is true while
		 * individual SOFT_OFF and HARD_OFF checks are false.
		 */
		if (chipset_in_state(CHIPSET_STATE_SOFT_OFF) ||
		    chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
			chipset_power_on();
		}
	}

	hook_call_deferred(&hang_detect_power_on_data,
			   HANG_DETECT_COLD_REBOOT_OFF_DELAY_US);
}

static void hang_detect_cold_reboot(void)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return;

	CPRINTS("Warm reset failed; forcing cold reboot");
	cold_reset_pending = true;
	power_on_retries = HANG_DETECT_COLD_REBOOT_MAX_RETRIES;
	chipset_force_shutdown(CHIPSET_RESET_HANG_REBOOT);
	hook_call_deferred(&hang_detect_power_on_data,
			   HANG_DETECT_COLD_REBOOT_OFF_DELAY_US);
}
DECLARE_DEFERRED(hang_detect_cold_reboot);

/**
 * hang detect handlers for reboot.
 */
static void hang_detect_reboot(void)
{
	uint16_t timeout_sec = reboot_timeout_sec;

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return;

	/*
	 * Warm reset the AP first. If the chipset fails to enter reset (which
	 * cancels hang_detect_cold_reboot via HOOK_CHIPSET_RESET) or recover
	 * within another reboot_timeout_sec window, escalate to a cold reboot
	 * (force shutdown + power on).
	 */
	CPRINTS("Triggering reboot");
	bootstatus = EC_HANG_DETECT_AP_BOOT_EC_WDT;
	chipset_reset(CHIPSET_RESET_HANG_REBOOT);
	hook_call_deferred(&hang_detect_cold_reboot_data, timeout_sec * SECOND);
}
DECLARE_DEFERRED(hang_detect_reboot);

static void hang_detect_reload(void)
{
	cold_reset_pending = false;
	power_on_retries = 0;
	hook_call_deferred(&hang_detect_cold_reboot_data, -1);
	hook_call_deferred(&hang_detect_power_on_data, -1);
	hook_call_deferred(&hang_detect_reboot_data,
			   reboot_timeout_sec * SECOND);
}

static void hang_detect_cancel(void)
{
	CPRINTS("Stop on AP request");
	cold_reset_pending = false;
	power_on_retries = 0;
	hook_call_deferred(&hang_detect_reboot_data, -1);
	hook_call_deferred(&hang_detect_cold_reboot_data, -1);
	hook_call_deferred(&hang_detect_power_on_data, -1);
}

static void hang_detect_deferred_stop(void)
{
	if (cold_reset_pending)
		return;

	CPRINTS("Stop on AP shutdown");
	power_on_retries = 0;
	hook_call_deferred(&hang_detect_reboot_data, -1);
	hook_call_deferred(&hang_detect_cold_reboot_data, -1);
	hook_call_deferred(&hang_detect_power_on_data, -1);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, hang_detect_deferred_stop,
	     HOOK_PRIO_DEFAULT);

static void hang_detect_reset(void)
{
	CPRINTS("Stop on Chipset reset");
	hook_call_deferred(&hang_detect_reboot_data, -1);
	hook_call_deferred(&hang_detect_cold_reboot_data, -1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, hang_detect_reset, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Host command */

static enum ec_status
hang_detect_host_command(struct host_cmd_handler_args *args)
{
	const struct ec_params_hang_detect *p = args->params;
	struct ec_response_hang_detect *r = args->response;
	enum ec_status ret = EC_RES_SUCCESS;
	enum chipset_shutdown_reason ec_reason;

	switch (p->command) {
	case EC_HANG_DETECT_CMD_RELOAD:
		/* Handle reload hang timer on request */
		if (reboot_timeout_sec < EC_HANG_DETECT_MIN_TIMEOUT) {
			CPRINTS("Reboot timeout has to be greater than %ds",
				EC_HANG_DETECT_MIN_TIMEOUT);
			ret = EC_RES_INVALID_PARAM;
			break;
		}
		hang_detect_reload();
		break;

	case EC_HANG_DETECT_CMD_CANCEL:
		/* Handle cancel hang timer on request */
		hang_detect_cancel();
		/* Clear reboot timeout - it must be set every watchdog setup */
		reboot_timeout_sec = 0;
		break;

	case EC_HANG_DETECT_CMD_SET_TIMEOUT:
		if (p->reboot_timeout_sec < EC_HANG_DETECT_MIN_TIMEOUT) {
			CPRINTS("Reboot timeout has to be greater than %ds",
				EC_HANG_DETECT_MIN_TIMEOUT);
			ret = EC_RES_INVALID_PARAM;
			break;
		}

		/* Cancel currently running AP hang detect timer */
		hang_detect_cancel();
		/* Save new reboot timeout */
		reboot_timeout_sec = p->reboot_timeout_sec;
		CPRINTS("reboot timeout: %d(s)", reboot_timeout_sec);
		break;

	case EC_HANG_DETECT_CMD_GET_STATUS:
		ec_reason = chipset_get_shutdown_reason();
		args->response_size = sizeof(*r);
		/**
		 * chipset_get_shutdown_reason() provides the last reason the EC
		 * has rebooted AP. It is not aware of any AP-initiated reboot
		 * or shutdown. For example, if EC-watchdog triggered the AP
		 * reboot and later the AP was powered off or rebooted (e.g.
		 * with reboot command or powered-off in UI) the
		 * chipset_get_shutdown_reason() will still return the
		 * CHIPSET_RESET_HANG_REBOOT as the last reset reason. To
		 * address this issue, the watchdog kernel module has a shutdown
		 * callback that sends EC_CMD_HANG_DETECT with
		 * EC_HANG_DETECT_CMD_CLEAR_STATUS set every time the AP is
		 * shutting down or rebooting gracefully (gracefully here means
		 * "not triggered by watchdog") to inform that AP is closing
		 * normally.
		 */
		if (ec_reason == CHIPSET_RESET_HANG_REBOOT &&
		    bootstatus == EC_HANG_DETECT_AP_BOOT_EC_WDT)
			r->status = EC_HANG_DETECT_AP_BOOT_EC_WDT;
		else
			r->status = EC_HANG_DETECT_AP_BOOT_NORMAL;
		CPRINTS("EC Watchdog status %d", r->status);
		break;

	case EC_HANG_DETECT_CMD_CLEAR_STATUS:
		CPRINTS("Clearing bootstatus");
		bootstatus = EC_HANG_DETECT_AP_BOOT_NORMAL;
		cold_reset_pending = false;
		power_on_retries = 0;
		hook_call_deferred(&hang_detect_cold_reboot_data, -1);
		hook_call_deferred(&hang_detect_power_on_data, -1);
		break;

	default:
		CPRINTS("Unknown command (%04x)", p->command);
		ret = EC_RES_INVALID_PARAM;
		break;
	}

	return ret;
}
DECLARE_HOST_COMMAND(EC_CMD_HANG_DETECT, hang_detect_host_command,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console command */

static int command_hang_detect(int argc, const char **argv)
{
	ccprintf("reboot timeout: %d(s)\n", reboot_timeout_sec);
	ccprintf("bootstatus: %02x\n", bootstatus);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hangdet, command_hang_detect, NULL,
			"Print hang detect state");
