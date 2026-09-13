/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_reset_log.h"
#include "chipset.h"
#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

static void ap_control_before(void *data)
{
	ARG_UNUSED(data);
	test_chipset_corrupt_reset_log_checksum();
	init_reset_log();
}

#if IS_ENABLED(CONFIG_PLATFORM_EC_HOSTCMD_AP_SHUTDOWN)
/* Test EC_CMD_AP_SHUTDOWN host command triggers chipset shutdown */
ZTEST(hc_ap_control, test_ap_shutdown)
{
	zassert_ok(ec_cmd_ap_shutdown(NULL));
	zassert_equal(CHIPSET_SHUTDOWN_HOST_CMD, chipset_get_shutdown_reason());
}
#endif

#if IS_ENABLED(CONFIG_PLATFORM_EC_HOSTCMD_AP_RESET)
/* Test EC_CMD_AP_RESET host command triggers chipset reset */
ZTEST(hc_ap_control, test_ap_reset)
{
	zassert_ok(ec_cmd_ap_reset(NULL));
	zassert_equal(CHIPSET_RESET_HOST_CMD, chipset_get_shutdown_reason());
}
#endif

#if IS_ENABLED(CONFIG_PLATFORM_EC_HOSTCMD_AP_RESET_SCHEDULED)
/* Test EC_CMD_AP_RESET_SCHEDULED with zero delay triggers immediate reset */
ZTEST(hc_ap_control, test_ap_reset_scheduled_immediate)
{
	struct ec_params_ap_reset_scheduled params = {
		.delay_ms = 0,
	};

	zassert_ok(ec_cmd_ap_reset_scheduled(NULL, &params));
	zassert_equal(CHIPSET_RESET_HOST_CMD, chipset_get_shutdown_reason());
}

/* Test EC_CMD_AP_RESET_SCHEDULED with non-zero delay schedules deferred reset
 */
ZTEST(hc_ap_control, test_ap_reset_scheduled_delayed)
{
	struct ec_params_ap_reset_scheduled params = {
		.delay_ms = 50,
	};

	zassert_ok(ec_cmd_ap_reset_scheduled(NULL, &params));

	/* Verify reset hasn't occurred immediately */
	zassert_not_equal(CHIPSET_RESET_HOST_CMD,
			  chipset_get_shutdown_reason());

	/* Wait for the deferred reset to fire. */
	zassert_true(WAIT_FOR(chipset_get_shutdown_reason() ==
				      CHIPSET_RESET_HOST_CMD,
			      500000, k_msleep(20)));
}
#endif

ZTEST_SUITE(hc_ap_control, drivers_predicate_post_main, NULL, ap_control_before,
	    NULL, NULL);
