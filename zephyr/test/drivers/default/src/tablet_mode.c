/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "tablet_mode.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

static void tabletmode_before(void *state)
{
	ARG_UNUSED(state);
	tablet_reset();
}

static void tabletmode_after(void *state)
{
	ARG_UNUSED(state);
	tablet_reset();
}

/**
 * @brief TestPurpose: various tablet_set_mode operations, make sure lid and
 * base works independently.
 */
ZTEST_USER(tabletmode, test_tablet_set_mode)
{
	int ret;

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexpected tablet initial mode: %d", ret);

	tablet_set_mode(1, TABLET_TRIGGER_LID);
	ret = tablet_get_mode();
	zassert_equal(ret, 1, "unexpected tablet mode: %d", ret);

	tablet_set_mode(1, TABLET_TRIGGER_BASE);
	ret = tablet_get_mode();
	zassert_equal(ret, 1, "unexpected tablet mode: %d", ret);

	/**
	 * Tablet mode should remain enabled, since both _LID and _BASE were set
	 * previously, and this only clears _LID.
	 */
	tablet_set_mode(0, TABLET_TRIGGER_LID);
	ret = tablet_get_mode();
	zassert_equal(ret, 1, "unexpected tablet mode: %d", ret);

	/**
	 * Both _LID and _BASE are now cleared, so DUT is no longer in tablet
	 * mode.
	 */
	tablet_set_mode(0, TABLET_TRIGGER_BASE);
	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexpected tablet mode: %d", ret);
}

/**
 * @brief TestPurpose: test the tablet_disable functionality.
 */
ZTEST_USER(tabletmode, test_tablet_disable)
{
	int ret;

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexpected tablet initial mode: %d", ret);

	tablet_disable();
	tablet_set_mode(1, TABLET_TRIGGER_LID);

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexpected tablet mode: %d", ret);
}

/**
 * @brief TestPurpose: check that tabletmode on and off changes the mode.
 */
ZTEST_USER(tabletmode, test_settabletmode_on_off)
{
	int ret;

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexpected tablet initial mode: %d", ret);

	ret = shell_execute_cmd(get_ec_shell(), "tabletmode");
	zassert_equal(ret, EC_SUCCESS, "unexpected command return status: %d",
		      ret);

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexpected tablet mode: %d", ret);

	ret = shell_execute_cmd(get_ec_shell(), "tabletmode on");
	zassert_equal(ret, EC_SUCCESS, "unexpected command return status: %d",
		      ret);

	ret = tablet_get_mode();
	zassert_equal(ret, 1, "unexpected tablet mode: %d", ret);

	ret = shell_execute_cmd(get_ec_shell(), "tabletmode off");
	zassert_equal(ret, EC_SUCCESS, "unexpected command return status: %d",
		      ret);

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexpected tablet mode: %d", ret);
}

/**
 * @brief TestPurpose: ensure that console tabletmode forces the status,
 * inhibiting tablet_set_mode, and then unforce it with reset.
 */
ZTEST_USER(tabletmode, test_settabletmode_forced)
{
	int ret;

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexpected tablet initial mode: %d", ret);

	ret = shell_execute_cmd(get_ec_shell(), "tabletmode on");
	zassert_equal(ret, EC_SUCCESS, "unexpected command return status: %d",
		      ret);

	ret = tablet_get_mode();
	zassert_equal(ret, 1, "unexpected tablet mode: %d", ret);

	tablet_set_mode(0, TABLET_TRIGGER_LID);

	ret = tablet_get_mode();
	zassert_equal(ret, 1, "unexpected tablet mode: %d", ret);

	ret = shell_execute_cmd(get_ec_shell(), "tabletmode reset");
	zassert_equal(ret, EC_SUCCESS, "unexpected command return status: %d",
		      ret);

	tablet_set_mode(0, TABLET_TRIGGER_LID);

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexpected tablet mode: %d", ret);
}

/**
 * @brief TestPurpose: check the "too many arguments" case.
 */
ZTEST_USER(tabletmode, test_settabletmode_too_many_args)
{
	int ret;

	ret = shell_execute_cmd(get_ec_shell(),
				"tabletmode too many arguments");
	zassert_equal(ret, EC_ERROR_PARAM_COUNT,
		      "unexpected command return status: %d", ret);
}

/**
 * @brief TestPurpose: check the "unknown argument" case.
 */
ZTEST_USER(tabletmode, test_settabletmode_unknown_arg)
{
	int ret;

	ret = shell_execute_cmd(get_ec_shell(), "tabletmode X");
	zassert_equal(ret, EC_ERROR_PARAM1,
		      "unexpected command return status: %d", ret);
}

/**
 * @brief TestPurpose: test EC_CMD_SET_TABLET_MODE host command.
 */
ZTEST(tabletmode, test_host_cmd_set_tablet_mode)
{
	struct ec_params_set_tablet_mode params;
	struct host_cmd_handler_args args = {
		.command = EC_CMD_SET_TABLET_MODE,
		.version = 0,
		.params = &params,
		.params_size = sizeof(params),
	};

	/* Force tablet mode via host command */
	params.tablet_mode = TABLET_MODE_FORCE_TABLET;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);
	zassert_equal(tablet_get_mode(), 1);

	/* Calling tablet_set_mode should be ignored while forced */
	tablet_set_mode(0, TABLET_TRIGGER_LID);
	zassert_equal(tablet_get_mode(), 1);

	/* Force clamshell mode */
	params.tablet_mode = TABLET_MODE_FORCE_CLAMSHELL;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);
	zassert_equal(tablet_get_mode(), 0);

	/* Reset to default mode */
	params.tablet_mode = TABLET_MODE_DEFAULT;
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);
	zassert_equal(tablet_get_mode(), 0);

	/* Invalid parameter */
	params.tablet_mode = 0xff;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);
}

/**
 * @brief TestPurpose: test tablet_disable edge cases and trigger flags.
 */
ZTEST(tabletmode, test_tablet_disable_and_triggers)
{
	/* 1. Disable when already in tablet mode */
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(tablet_get_mode(), 1);
	tablet_disable();
	zassert_equal(tablet_get_mode(), 0);

	/* 2. Call disable again when already disabled (no-op) */
	tablet_disable();
	zassert_equal(tablet_get_mode(), 0);

	/* 3. Calling tablet_set_mode when disabled */
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(tablet_get_mode(), 0);

	/* 4. Calling tablet_set_mode when disabled and forced */
	shell_execute_cmd(get_ec_shell(), "tabletmode on");
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	shell_execute_cmd(get_ec_shell(), "tabletmode reset");

	/* 5. Test TABLET_TRIGGER_OVERRIDE_GMR */
	tablet_reset();
	tablet_set_mode(1, TABLET_TRIGGER_OVERRIDE_GMR | TABLET_TRIGGER_LID);
	zassert_equal(tablet_get_mode(), 1);
	tablet_set_mode(0, TABLET_TRIGGER_OVERRIDE_GMR | TABLET_TRIGGER_LID);
	zassert_equal(tablet_get_mode(), 0);
}

ZTEST_SUITE(tabletmode, drivers_predicate_post_main, NULL, tabletmode_before,
	    tabletmode_after, NULL);
