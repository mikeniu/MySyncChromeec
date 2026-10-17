/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_reset_log.h"
#include "hooks.h"
#include "host_command.h"
#include "mock/power.h"
#include "power.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"

#include <zephyr/ztest.h>

static void set_timeout(uint16_t timeout)
{
	struct ec_params_hang_detect req = {
		.command = EC_HANG_DETECT_CMD_SET_TIMEOUT,
		.reboot_timeout_sec = timeout
	};
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
}

ZTEST_USER(ap_hang_detect, test_set_parms_good_timeout)
{
	set_timeout(EC_HANG_DETECT_MIN_TIMEOUT);
}

ZTEST_USER(ap_hang_detect, test_set_parms_bad_timeout)
{
	struct ec_params_hang_detect req = {
		.command = EC_HANG_DETECT_CMD_SET_TIMEOUT,
		.reboot_timeout_sec = EC_HANG_DETECT_MIN_TIMEOUT - 1
	};
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	zassert_equal(ec_cmd_hang_detect(&args, &req, &resp),
		      EC_RES_INVALID_PARAM);

	req.reboot_timeout_sec = (INT32_MAX / USEC_PER_SEC) + 1;
	zassert_equal(ec_cmd_hang_detect(&args, &req, &resp),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER(ap_hang_detect, test_cancel)
{
	struct ec_params_hang_detect req;
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	memset(&req, 0, sizeof(req));

	/* Confirm the AP booted normally */
	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_NORMAL);

	set_timeout(EC_HANG_DETECT_MIN_TIMEOUT);
	req.command = EC_HANG_DETECT_CMD_RELOAD;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));

	/* let's wait 1s and then cancel watchdog */
	k_sleep(K_SECONDS(1));
	req.command = EC_HANG_DETECT_CMD_CANCEL;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));

	/* wait and check if watchdog has rebooted the AP */
	k_sleep(K_SECONDS(30));

	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_NORMAL);
}

ZTEST_USER(ap_hang_detect, test_chipset_reset_cancels_watchdog)
{
	struct ec_params_hang_detect req = { 0 };
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_NORMAL);

	set_timeout(EC_HANG_DETECT_MIN_TIMEOUT);
	req.command = EC_HANG_DETECT_CMD_RELOAD;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));

	k_sleep(K_SECONDS(1));
	hook_notify(HOOK_CHIPSET_RESET);

	k_sleep(K_SECONDS(2 * EC_HANG_DETECT_MIN_TIMEOUT));

	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_NORMAL);
}

static void reload_and_get_status(void)
{
	struct ec_params_hang_detect req;
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	test_set_chipset_to_s0();

	memset(&req, 0, sizeof(req));

	/* Confirm the AP booted normally */
	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_NORMAL);

	/* Set timeout, reload the timer and don't pet the watchdog */
	set_timeout(EC_HANG_DETECT_MIN_TIMEOUT);
	req.command = EC_HANG_DETECT_CMD_RELOAD;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	k_sleep(K_SECONDS(EC_HANG_DETECT_MIN_TIMEOUT + 1));

	/* EC should warm-reset the AP and set the watchdog status */
	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_EC_WDT);

	/*
	 * Verify that HOOK_CHIPSET_RESET (triggered when PLTRST# toggles
	 * after Stage 1 chipset_reset()) does NOT cancel Stage 2 cold reboot.
	 */
	hook_notify(HOOK_CHIPSET_RESET);

	/*
	 * If the AP remains unresponsive after the warm reset, the EC
	 * escalates to a cold reboot (chipset_force_shutdown -> POWER_G3 ->
	 * chipset_power_on -> POWER_S0) and preserves the EC watchdog reset
	 * status. Wait for Stage 2 timeout (5s) + mock power S5 inactivity
	 * timeout (10s to reach POWER_G3) + power-on back to POWER_S0.
	 */
	k_sleep(K_SECONDS(EC_HANG_DETECT_MIN_TIMEOUT + 14));
	zassert_equal(POWER_S0, power_get_state());
	zassert_equal(CHIPSET_RESET_HANG_REBOOT, chipset_get_shutdown_reason());
	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_EC_WDT);
}

ZTEST_USER(ap_hang_detect, test_reload_and_get_status)
{
	reload_and_get_status();
}

ZTEST_USER(ap_hang_detect, test_clear_status)
{
	struct ec_params_hang_detect req;
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	memset(&req, 0, sizeof(req));

	/* re-use reload_and_get_status() to set
	 * EC_HANG_DETECT_AP_BOOT_EC_WDT status
	 */
	reload_and_get_status();

	req.command = EC_HANG_DETECT_CMD_CLEAR_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_NORMAL);
}

ZTEST_USER(ap_hang_detect, test_stage1_recovery_cancels_cold_reboot)
{
	struct ec_params_hang_detect req = { 0 };
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	test_set_chipset_to_s0();
	req.command = EC_HANG_DETECT_CMD_CLEAR_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));

	/* Arm watchdog and let Stage 1 warm reset fire at T+5s */
	set_timeout(EC_HANG_DETECT_MIN_TIMEOUT);
	req.command = EC_HANG_DETECT_CMD_RELOAD;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	k_sleep(K_SECONDS(EC_HANG_DETECT_MIN_TIMEOUT + 1));

	/* Simulate kernel cros_ec_wdt_probe() reading and clearing status */
	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_EC_WDT);

	req.command = EC_HANG_DETECT_CMD_CLEAR_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));

	/*
	 * Verify Stage 2 cold reset does NOT fire at T+10s: if
	 * hang_detect_cold_reboot() fired, chipset_force_shutdown() would
	 * transition the chipset to POWER_S5 (for the 10s S5 inactivity
	 * window).
	 */
	k_sleep(K_SECONDS(EC_HANG_DETECT_MIN_TIMEOUT + 2));
	zassert_equal(POWER_S0, power_get_state());

	/*
	 * Also verify that EC_HANG_DETECT_CMD_RELOAD during the Stage 1
	 * warm-reset window cancels Stage 2 cold reboot and re-arms Stage 1.
	 */
	set_timeout(EC_HANG_DETECT_MIN_TIMEOUT);
	req.command = EC_HANG_DETECT_CMD_RELOAD;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	k_sleep(K_SECONDS(EC_HANG_DETECT_MIN_TIMEOUT + 1));

	/* Reload before Stage 2 fires, then cancel before the new Stage 1 */
	req.command = EC_HANG_DETECT_CMD_RELOAD;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	k_sleep(K_SECONDS(1));
	req.command = EC_HANG_DETECT_CMD_CANCEL;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	k_sleep(K_SECONDS(EC_HANG_DETECT_MIN_TIMEOUT + 2));
	zassert_equal(POWER_S0, power_get_state());
	req.command = EC_HANG_DETECT_CMD_CLEAR_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
}

ZTEST_USER(ap_hang_detect, test_cold_reboot_g3_timeout_and_s5_followup)
{
	struct ec_params_hang_detect req = { 0 };
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	/*
	 * 1. If chipset_force_shutdown fails to reach POWER_G3 within
	 * HANG_DETECT_COLD_REBOOT_MAX_RETRIES (20s), hang_detect_power_on
	 * logs an error and gives up: entering POWER_G3 after the 20s retry
	 * window must not power the AP back on.
	 */
	test_set_chipset_to_s0();
	req.command = EC_HANG_DETECT_CMD_CLEAR_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));

	set_timeout(EC_HANG_DETECT_MIN_TIMEOUT);
	req.command = EC_HANG_DETECT_CMD_RELOAD;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	/* Wait for Stage 1 (5s) + Stage 2 (5s) + 1s into POWER_S5 */
	k_sleep(K_SECONDS(2 * EC_HANG_DETECT_MIN_TIMEOUT + 1));
	zassert_equal(POWER_S5, power_get_state());
	/*
	 * Prevent S5 -> G3 transition so hang_detect_power_on exhausts retries.
	 */
	power_set_state(POWER_S0);
	k_sleep(K_SECONDS(22));
	/* Entering G3 after retries are exhausted must not trigger power-on */
	test_set_chipset_to_g3();
	k_sleep(K_SECONDS(2));
	zassert_equal(POWER_G3, power_get_state());
	test_set_chipset_to_s0();

	/*
	 * 2. If G3 -> S5 rail ramp stops in POWER_S5 after the initial
	 * chipset_power_on() call in POWER_G3, the 1s follow-up check
	 * calls chipset_power_on() in POWER_S5 to wake the AP to POWER_S0.
	 */
	set_timeout(EC_HANG_DETECT_MIN_TIMEOUT);
	req.command = EC_HANG_DETECT_CMD_RELOAD;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	/* Wait for Stage 1 (5s) + Stage 2 (5s) + 500ms */
	k_sleep(K_MSEC(2 * EC_HANG_DETECT_MIN_TIMEOUT * 1000 + 500));
	/* Reach POWER_G3 before the first 1s poll at T+11s */
	power_set_state(POWER_G3);
	k_sleep(K_MSEC(700));
	/*
	 * At T+11.2s, hang_detect_power_on() has seen POWER_G3, restored
	 * CHIPSET_RESET_HANG_REBOOT, and armed the 1s follow-up for T+12s.
	 * Force POWER_S5 (clearing any pending wake) to simulate a slow
	 * G3 -> S5 ramp that missed the initial power-on pulse.
	 */
	zassert_equal(CHIPSET_RESET_HANG_REBOOT, chipset_get_shutdown_reason());
	power_set_state(POWER_S5);
	k_sleep(K_SECONDS(2));
	zassert_equal(POWER_S0, power_get_state());

	req.command = EC_HANG_DETECT_CMD_CLEAR_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
}

ZTEST_USER(ap_hang_detect, test_ap_off_ignores_hang_detect)
{
	struct ec_params_hang_detect req = { 0 };
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	/*
	 * 1. If the AP shuts down to G3 before hang_detect_reboot() fires,
	 * the EC must not reset or wake the AP.
	 */
	test_set_chipset_to_s0();
	req.command = EC_HANG_DETECT_CMD_CLEAR_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));

	set_timeout(EC_HANG_DETECT_MIN_TIMEOUT);
	req.command = EC_HANG_DETECT_CMD_RELOAD;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));

	test_set_chipset_to_g3();
	k_sleep(K_SECONDS(EC_HANG_DETECT_MIN_TIMEOUT + 1));
	zassert_equal(POWER_G3, power_get_state());
	req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	zassert_equal(resp.status, EC_HANG_DETECT_AP_BOOT_NORMAL);

	/*
	 * 2. If the AP shuts down to G3 during the warm-reset window before
	 * hang_detect_cold_reboot() fires, the EC must not power it back on.
	 */
	test_set_chipset_to_s0();
	set_timeout(EC_HANG_DETECT_MIN_TIMEOUT);
	req.command = EC_HANG_DETECT_CMD_RELOAD;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	k_sleep(K_SECONDS(EC_HANG_DETECT_MIN_TIMEOUT + 1));

	test_set_chipset_to_g3();
	k_sleep(K_SECONDS(EC_HANG_DETECT_MIN_TIMEOUT + 2));
	zassert_equal(POWER_G3, power_get_state());

	req.command = EC_HANG_DETECT_CMD_CLEAR_STATUS;
	zassert_ok(ec_cmd_hang_detect(&args, &req, &resp));
	test_set_chipset_to_s0();
}

ZTEST_USER(ap_hang_detect, test_bad_command)
{
	struct ec_params_hang_detect req = {
		/* EC_HANG_DETECT_CMD_CLEAR_STATUS is the last command */
		.command = EC_HANG_DETECT_CMD_CLEAR_STATUS + 1,
	};
	struct ec_response_hang_detect resp;
	struct host_cmd_handler_args args;

	zassert_equal(ec_cmd_hang_detect(&args, &req, &resp),
		      EC_RES_INVALID_PARAM);
}

ZTEST_SUITE(ap_hang_detect, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
