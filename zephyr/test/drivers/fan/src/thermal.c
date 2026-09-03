/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "ec_commands.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "temp_sensor.h"
#include "temp_sensor/temp_sensor.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "thermal.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#define GPIO_PG_EC_DSW_PWROK_PATH NAMED_GPIOS_GPIO_NODE(pg_ec_dsw_pwrok)
#define GPIO_PG_EC_DSW_PWROK_PORT DT_GPIO_PIN(GPIO_PG_EC_DSW_PWROK_PATH, gpios)

#define GPIO_EC_PG_PIN_TEMP_PATH NAMED_GPIOS_GPIO_NODE(ec_pg_pin_temp)
#define GPIO_EC_PG_PIN_TEMP_PORT DT_GPIO_PIN(GPIO_EC_PG_PIN_TEMP_PATH, gpios)

#define GPIO_PG_FAN_PATH NAMED_GPIOS_GPIO_NODE(test)
#define GPIO_PG_FAN_PORT DT_GPIO_PIN(GPIO_PG_FAN_PATH, gpios)

#define THERMAL_PARAMS_SIZE \
	(sizeof(struct ec_thermal_config) * TEMP_SENSOR_COUNT)

struct thermal_fixture {
	const struct device *pg_dsw_dev;
	const struct device *pg_temp_dev;
	const struct device *pg_fan_dev;
	struct ec_thermal_config initial_thermal_params[TEMP_SENSOR_COUNT];
};
static struct thermal_fixture fixture;

static void *thermal_setup(void)
{
	fixture.pg_dsw_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_PG_EC_DSW_PWROK_PATH, gpios));
	fixture.pg_temp_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_EC_PG_PIN_TEMP_PATH, gpios));
	fixture.pg_fan_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_PG_FAN_PATH, gpios));

	memcpy(fixture.initial_thermal_params, thermal_params,
	       THERMAL_PARAMS_SIZE);

	return &fixture;
}

static void thermal_before(void *data)
{
	struct thermal_fixture *fixture = (struct thermal_fixture *)data;

	test_set_chipset_to_s0();
	fan_set_count(CONFIG_FANS);

	zassert_ok(gpio_emul_input_set(fixture->pg_dsw_dev,
				       GPIO_PG_EC_DSW_PWROK_PORT, 1));
	zassert_ok(gpio_emul_input_set(fixture->pg_temp_dev,
				       GPIO_EC_PG_PIN_TEMP_PORT, 1));
	zassert_ok(
		gpio_emul_input_set(fixture->pg_fan_dev, GPIO_PG_FAN_PORT, 1));

	memcpy(thermal_params, fixture->initial_thermal_params,
	       THERMAL_PARAMS_SIZE);
}

static void thermal_after(void *data)
{
	struct thermal_fixture *fixture = (struct thermal_fixture *)data;

	test_set_chipset_to_s0();
	fan_set_count(CONFIG_FANS);

	zassert_ok(gpio_emul_input_set(fixture->pg_dsw_dev,
				       GPIO_PG_EC_DSW_PWROK_PORT, 1));
	zassert_ok(gpio_emul_input_set(fixture->pg_temp_dev,
				       GPIO_EC_PG_PIN_TEMP_PORT, 1));
	zassert_ok(
		gpio_emul_input_set(fixture->pg_fan_dev, GPIO_PG_FAN_PORT, 1));

	memcpy(thermal_params, fixture->initial_thermal_params,
	       THERMAL_PARAMS_SIZE);

	hook_notify(HOOK_SECOND);
}

ZTEST_SUITE(thermal, drivers_predicate_post_main, thermal_setup, thermal_before,
	    thermal_after, NULL);

ZTEST(thermal, test_thermal_fan_percent)
{
	/* cur < low */
	zassert_equal(thermal_fan_percent(100, 200, 50), 0);
	zassert_equal(thermal_fan_percent(100, 200, 99), 0);
	zassert_equal(thermal_fan_percent(100, 200, 100), 0);

	/* cur > high */
	zassert_equal(thermal_fan_percent(100, 200, 200), 100);
	zassert_equal(thermal_fan_percent(100, 200, 201), 100);
	zassert_equal(thermal_fan_percent(100, 200, 300), 100);

	/* intermediate values */
	zassert_equal(thermal_fan_percent(100, 200, 125), 25);
	zassert_equal(thermal_fan_percent(100, 200, 150), 50);
	zassert_equal(thermal_fan_percent(100, 200, 175), 75);
}

ZTEST(thermal, test_thermal_control_no_sensors_read)
{
	/* Unpower sensors so none can be read */
	zassert_ok(gpio_emul_input_set(fixture.pg_temp_dev,
				       GPIO_EC_PG_PIN_TEMP_PORT, 0));

	/* In S0 (not hard off): triggers smi_sensor_failure_warning */
	test_set_chipset_to_s0();
	hook_notify(HOOK_SECOND);

	/* In G3 (hard off): does not trigger smi warning */
	test_set_chipset_to_g3();
	hook_notify(HOOK_SECOND);
}

ZTEST(thermal, test_thermal_control_fan)
{
	int t = 0;

	zassert_ok(temp_sensor_read(0, &t));
	zassert_true(t > 0);

	/* Clear other sensors so sensor 0 dictates fan */
	memset(thermal_params, 0, THERMAL_PARAMS_SIZE);

	/* Case 1: Temp within range -> ~50% */
	thermal_params[0].temp_fan_off = t - 20;
	thermal_params[0].temp_fan_max = t + 20;
	hook_notify(HOOK_SECOND);
	/* Target RPM should be ~50% */
	zassert_equal(fan_get_rpm_target(0), fan_percent_to_rpm(0, 50));

	/* Case 2: Temp below fan_off -> 0% */
	thermal_params[0].temp_fan_off = t + 10;
	thermal_params[0].temp_fan_max = t + 20;
	hook_notify(HOOK_SECOND);
	zassert_equal(fan_get_rpm_target(0), 0);

	/* Case 3: Temp above fan_max -> 100% */
	thermal_params[0].temp_fan_off = t - 20;
	thermal_params[0].temp_fan_max = t - 10;
	hook_notify(HOOK_SECOND);
	zassert_equal(fan_get_rpm_target(0), fans[0].rpm->rpm_max);
}

ZTEST(thermal, test_thermal_control_thresholds)
{
	int t = 0;

	zassert_ok(temp_sensor_read(0, &t));
	zassert_true(t > 0);

	/* Clear other sensors so only sensor 0 is active */
	memset(thermal_params, 0, THERMAL_PARAMS_SIZE);

	/* Test WARN threshold */
	thermal_params[0].temp_host[EC_TEMP_THRESH_WARN] = t - 5;
	hook_notify(HOOK_SECOND);

	/* Cool down WARN */
	thermal_params[0].temp_host[EC_TEMP_THRESH_WARN] = t + 10;
	hook_notify(HOOK_SECOND);

	/* Test HIGH threshold */
	thermal_params[0].temp_host[EC_TEMP_THRESH_HIGH] = t - 5;
	hook_notify(HOOK_SECOND);

	/* Cool down HIGH */
	thermal_params[0].temp_host[EC_TEMP_THRESH_HIGH] = t + 10;
	hook_notify(HOOK_SECOND);

	/* Test HALT threshold */
	thermal_params[0].temp_host[EC_TEMP_THRESH_HALT] = t - 5;
	hook_notify(HOOK_SECOND);

	/* Halting triggers chipset_force_shutdown. Restore S0. */
	test_set_chipset_to_s0();

	/* Cool down HALT */
	thermal_params[0].temp_host[EC_TEMP_THRESH_HALT] = t + 10;
	hook_notify(HOOK_SECOND);
}

ZTEST(thermal, test_thermal_control_release_hysteresis)
{
	int t = 0;

	zassert_ok(temp_sensor_read(0, &t));
	zassert_true(t > 0);

	memset(thermal_params, 0, THERMAL_PARAMS_SIZE);

	/* Limit higher than t, release lower than t (in hysteresis band) */
	thermal_params[0].temp_host[EC_TEMP_THRESH_WARN] = t + 10;
	thermal_params[0].temp_host_release[EC_TEMP_THRESH_WARN] = t - 5;
	hook_notify(HOOK_SECOND);

	/* t < release */
	thermal_params[0].temp_host_release[EC_TEMP_THRESH_WARN] = t + 15;
	hook_notify(HOOK_SECOND);
}

ZTEST(thermal, test_console_thermalget)
{
	CHECK_CONSOLE_CMD("thermalget", "sensor", EC_SUCCESS);
}

ZTEST(thermal, test_console_thermalset)
{
	/* Invalid param counts */
	CHECK_CONSOLE_CMD("thermalset", NULL, EC_ERROR_PARAM_COUNT);
	CHECK_CONSOLE_CMD("thermalset 0", NULL, EC_ERROR_PARAM_COUNT);
	CHECK_CONSOLE_CMD("thermalset 0 1 2 3 4 5 6 7", NULL,
			  EC_ERROR_PARAM_COUNT);

	/* Invalid parameters */
	CHECK_CONSOLE_CMD("thermalset abc 1 2", NULL, EC_ERROR_PARAM1);
	CHECK_CONSOLE_CMD("thermalset 0 bad", NULL, EC_ERROR_PARAM2);

	/* Valid set */
	CHECK_CONSOLE_CMD("thermalset 0 300 320 340 290 310", NULL, EC_SUCCESS);
	zassert_equal(thermal_params[0].temp_host[EC_TEMP_THRESH_WARN], 300);
	zassert_equal(thermal_params[0].temp_host[EC_TEMP_THRESH_HIGH], 320);
	zassert_equal(thermal_params[0].temp_host[EC_TEMP_THRESH_HALT], 340);
	zassert_equal(thermal_params[0].temp_fan_off, 290);
	zassert_equal(thermal_params[0].temp_fan_max, 310);

	/* Skip fields using -1 */
	CHECK_CONSOLE_CMD("thermalset 0 -1 -1 -1 -1 -1", NULL, EC_SUCCESS);
	zassert_equal(thermal_params[0].temp_host[EC_TEMP_THRESH_WARN], 300);
	zassert_equal(thermal_params[0].temp_host[EC_TEMP_THRESH_HIGH], 320);
	zassert_equal(thermal_params[0].temp_host[EC_TEMP_THRESH_HALT], 340);
	zassert_equal(thermal_params[0].temp_fan_off, 290);
	zassert_equal(thermal_params[0].temp_fan_max, 310);
}

ZTEST(thermal, test_hc_thermal_thresholds)
{
	struct ec_params_thermal_set_threshold_v1 set_p = {
		.sensor_num = 0,
		.cfg = {
			.temp_host = { 301, 321, 341 },
			.temp_host_release = { 291, 311, 331 },
			.temp_fan_off = 281,
			.temp_fan_max = 311,
		},
	};
	struct ec_params_thermal_get_threshold_v1 get_p = {
		.sensor_num = 0,
	};
	struct ec_thermal_config get_r;

	/* Bad sensor num for SET */
	set_p.sensor_num = TEMP_SENSOR_COUNT;
	{
		struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
			EC_CMD_THERMAL_SET_THRESHOLD, 1, set_p);
		zassert_equal(host_command_process(&args),
			      EC_RES_INVALID_PARAM);
	}

	/* Bad sensor num for GET */
	get_p.sensor_num = TEMP_SENSOR_COUNT;
	{
		struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
			EC_CMD_THERMAL_GET_THRESHOLD, 1, get_r, get_p);
		zassert_equal(host_command_process(&args),
			      EC_RES_INVALID_PARAM);
	}

	/* Valid SET */
	set_p.sensor_num = 0;
	{
		struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
			EC_CMD_THERMAL_SET_THRESHOLD, 1, set_p);
		zassert_ok(host_command_process(&args));
	}

	/* Valid GET */
	get_p.sensor_num = 0;
	{
		struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
			EC_CMD_THERMAL_GET_THRESHOLD, 1, get_r, get_p);
		zassert_ok(host_command_process(&args));
		zassert_equal(args.response_size, sizeof(get_r));
		zassert_mem_equal(&get_r, &set_p.cfg, sizeof(get_r));
	}
}
