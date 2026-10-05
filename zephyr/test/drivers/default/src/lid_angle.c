/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_angle.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "tablet_mode.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <math.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LID_ANGLE_MIN_LARGE_ANGLE 0
#define LID_ANGLE_MAX_LARGE_ANGLE 360

int emul_lid_open(void);

static void lid_angle_after(void *f)
{
	ARG_UNUSED(f);
	/* Reset the wake angle */
	lid_angle_set_wake_angle(180);
	/* Flush the buffer */
	lid_angle_update(LID_ANGLE_UNRELIABLE);
	lid_angle_update(LID_ANGLE_UNRELIABLE);
	lid_angle_update(LID_ANGLE_UNRELIABLE);
	lid_angle_update(LID_ANGLE_UNRELIABLE);
}

ZTEST_SUITE(lid_angle, drivers_predicate_post_main, NULL, NULL, lid_angle_after,
	    NULL);

ZTEST(lid_angle, test_get_set_wake_angle)
{
	lid_angle_set_wake_angle(LID_ANGLE_MIN_LARGE_ANGLE - 1);
	zassert_equal(LID_ANGLE_MIN_LARGE_ANGLE, lid_angle_get_wake_angle(),
		      NULL);

	lid_angle_set_wake_angle(LID_ANGLE_MAX_LARGE_ANGLE + 1);
	zassert_equal(LID_ANGLE_MAX_LARGE_ANGLE, lid_angle_get_wake_angle(),
		      NULL);

	lid_angle_set_wake_angle(
		(LID_ANGLE_MIN_LARGE_ANGLE + LID_ANGLE_MAX_LARGE_ANGLE) / 2);
	zassert_equal((LID_ANGLE_MIN_LARGE_ANGLE + LID_ANGLE_MAX_LARGE_ANGLE) /
			      2,
		      lid_angle_get_wake_angle(), NULL);
}

ZTEST(lid_angle, test_no_wake_min_large_angle)
{
	lid_angle_set_wake_angle(LID_ANGLE_MIN_LARGE_ANGLE);
	lid_angle_update(45);
	lid_angle_update(45);
	lid_angle_update(45);
	lid_angle_update(45);

	zassert_equal(1, lid_angle_peripheral_enable_fake.call_count, NULL);
	zassert_equal(0, lid_angle_peripheral_enable_fake.arg0_val, NULL);
}

ZTEST(lid_angle, test_wake_max_large_angle)
{
	lid_angle_set_wake_angle(LID_ANGLE_MAX_LARGE_ANGLE);
	lid_angle_update(45);
	lid_angle_update(45);
	lid_angle_update(45);
	lid_angle_update(45);

	zassert_equal(1, lid_angle_peripheral_enable_fake.call_count, NULL);
	zassert_equal(1, lid_angle_peripheral_enable_fake.arg0_val, NULL);
}

#ifdef CONFIG_TABLET_MODE
#define ONE_G_16BIT 16384

int emul_lid_close(void);
int board_is_lid_angle_tablet_mode(void);
enum ec_status host_cmd_motion_lid(struct host_cmd_handler_args *args);

static void set_mock_sensors_angle(double angle_deg)
{
	int base_id = SENSOR_ID(DT_NODELABEL(base_accel));
	int lid_id = SENSOR_ID(DT_NODELABEL(lid_accel));

	zassert_true(base_id < SENSOR_COUNT, "base_id out of bounds");
	zassert_true(lid_id < SENSOR_COUNT, "lid_id out of bounds");

	motion_sensors[base_id].current_range = 2;
	motion_sensors[lid_id].current_range = 2;

	motion_sensors[base_id].xyz[X] = 0;
	motion_sensors[base_id].xyz[Y] = 0;
	motion_sensors[base_id].xyz[Z] = ONE_G_16BIT;

	double rad = angle_deg * M_PI / 180.0;

	motion_sensors[lid_id].xyz[X] = 0;
	motion_sensors[lid_id].xyz[Y] = (int16_t)(ONE_G_16BIT * sin(rad));
	motion_sensors[lid_id].xyz[Z] = (int16_t)(-ONE_G_16BIT * cos(rad));
}

ZTEST(lid_angle, test_symmetric_debounce)
{
	console_channel_enable("motionlid");
	emul_lid_open();
	k_sleep(K_MSEC(100));

	/*
	 * Transition to intermediate angle (180 deg) so that subsequent
	 * angle is set cleanly without triggering boundary debounce.
	 */
	set_mock_sensors_angle(180.0);
	motion_lid_calc();

	/* Small-to-large: initial angle 20 deg, noisy reading 335 deg */
	set_mock_sensors_angle(20.0);
	motion_lid_calc();
	zassert_within(motion_lid_get_angle(), 20, 2,
		       "Initial angle should be ~20, got %d",
		       motion_lid_get_angle());

	/* With symmetric debounce, it should correct 335 to 25 (360 - 335) */
	set_mock_sensors_angle(335.0);
	motion_lid_calc();
	zassert_within(motion_lid_get_angle(), 25, 2,
		       "Noisy 335 should be debounced to ~25, got %d",
		       motion_lid_get_angle());

	/*
	 * Transition to intermediate angle (180 deg) to reset the state away
	 * from 0/360 boundary before testing the large-to-small transition.
	 */
	set_mock_sensors_angle(180.0);
	motion_lid_calc();

	/* Large-to-small: enter tablet mode at 340 deg */
	set_mock_sensors_angle(340.0);
	for (int i = 0; i < (TABLET_MODE_DEBOUNCE_COUNT + 1); i++)
		motion_lid_calc();
	zassert_equal(tablet_get_mode(), 1, "Should be in tablet mode");
	zassert_within(motion_lid_get_angle(), 340, 2,
		       "Initial angle should be ~340, got %d",
		       motion_lid_get_angle());

	/* In tablet mode, it should correct noisy 25 to 335 (360 - 25) */
	set_mock_sensors_angle(25.0);
	motion_lid_calc();
	zassert_within(motion_lid_get_angle(), 335, 2,
		       "Noisy 25 should be debounced to ~335, got %d",
		       motion_lid_get_angle());

	/*
	 * Transition back to clamshell through 90 deg (< 160 deg threshold)
	 * so that tablet mode is exited before testing clamshell latch-up
	 * prevention.
	 */
	set_mock_sensors_angle(90.0);
	for (int i = 0; i < (TABLET_MODE_DEBOUNCE_COUNT + 1); i++)
		motion_lid_calc();
	zassert_equal(tablet_get_mode(), 0, "Must return to clamshell mode");

	/* Set angle to 20 deg in clamshell */
	set_mock_sensors_angle(20.0);
	motion_lid_calc();
	zassert_within(motion_lid_get_angle(), 20, 2,
		       "Angle must stay ~20 deg in clamshell, got %d",
		       motion_lid_get_angle());

	/* Noisy reading at 335 deg in clamshell should be debounced to 25 deg
	 */
	set_mock_sensors_angle(335.0);
	motion_lid_calc();
	zassert_within(motion_lid_get_angle(), 25, 2,
		       "Noisy 335 should be debounced to ~25, got %d",
		       motion_lid_get_angle());

	/* Next clean reading at 20 deg must stay 20 deg */
	set_mock_sensors_angle(20.0);
	motion_lid_calc();
	zassert_within(motion_lid_get_angle(), 20, 2,
		       "Angle must stay ~20 deg in clamshell, got %d",
		       motion_lid_get_angle());
	zassert_equal(tablet_get_mode(), 0, "Must stay in clamshell mode");
}

ZTEST(lid_angle, test_lid_angle_cycle_and_tablet_mode)
{
	int base_id = SENSOR_ID(DT_NODELABEL(base_accel));
	int lid_id = SENSOR_ID(DT_NODELABEL(lid_accel));

	zassert_equal(board_is_lid_angle_tablet_mode(), 1);

	/* 1. Start with lid closed at 0 degrees */
	emul_lid_close();
	k_sleep(K_MSEC(50));
	motion_sensors[base_id].current_range = 2;
	motion_sensors[lid_id].current_range = 2;
	motion_sensors[base_id].xyz[X] = 0;
	motion_sensors[base_id].xyz[Y] = 0;
	motion_sensors[base_id].xyz[Z] = ONE_G_16BIT;
	motion_sensors[lid_id].xyz[X] = 0;
	motion_sensors[lid_id].xyz[Y] = 0;
	motion_sensors[lid_id].xyz[Z] = -ONE_G_16BIT;

	motion_lid_calc();
	zassert_equal(motion_lid_get_angle(), 0);
	zassert_equal(tablet_get_mode(), 0);

	/* 2. Open lid to 90 degrees */
	emul_lid_open();
	k_sleep(K_MSEC(50));
	set_mock_sensors_angle(90.0);
	motion_lid_calc();
	zassert_within(motion_lid_get_angle(), 90, 2);
	zassert_equal(tablet_get_mode(), 0);

	/* 3. Open lid to 225 degrees (tablet mode zone) and debounce */
	set_mock_sensors_angle(225.0);
	for (int i = 0; i <= TABLET_MODE_DEBOUNCE_COUNT + 1; i++) {
		motion_lid_calc();
	}
	zassert_within(motion_lid_get_angle(), 225, 2);
	zassert_equal(tablet_get_mode(), 1);

	/* 4. Open lid to 350 degrees */
	set_mock_sensors_angle(350.0);
	motion_lid_calc();
	zassert_within(motion_lid_get_angle(), 350, 2);
	zassert_equal(tablet_get_mode(), 1);

	/* 5. Set lid angle to 10 deg while lid switch is open (unreliable) */
	set_mock_sensors_angle(10.0);
	motion_lid_calc();
	zassert_equal(motion_lid_get_angle(), LID_ANGLE_UNRELIABLE);
	zassert_equal(tablet_get_mode(), 1);

	/* 6. Rotate to 180 degrees */
	set_mock_sensors_angle(180.0);
	motion_lid_calc();
	zassert_within(motion_lid_get_angle(), 180, 2);
	zassert_equal(tablet_get_mode(), 1);

	/* 7. Close lid and return to 0 degrees (clamshell mode after debounce)
	 */
	emul_lid_close();
	k_sleep(K_MSEC(50));
	motion_sensors[base_id].xyz[X] = 0;
	motion_sensors[base_id].xyz[Y] = 0;
	motion_sensors[base_id].xyz[Z] = ONE_G_16BIT;
	motion_sensors[lid_id].xyz[X] = 0;
	motion_sensors[lid_id].xyz[Y] = 0;
	motion_sensors[lid_id].xyz[Z] = -ONE_G_16BIT;

	for (int i = 0; i <= TABLET_MODE_DEBOUNCE_COUNT + 1; i++) {
		motion_lid_calc();
	}
	zassert_equal(motion_lid_get_angle(), 0);
	zassert_equal(tablet_get_mode(), 0);

	/* 8. Large angle (350 deg) while lid is closed is unreliable */
	set_mock_sensors_angle(350.0);
	motion_lid_calc();
	zassert_equal(motion_lid_get_angle(), LID_ANGLE_UNRELIABLE);
	zassert_equal(tablet_get_mode(), 0);

	/* 9. Open lid again and transition through 180 to 350 deg */
	emul_lid_open();
	k_sleep(K_MSEC(50));
	set_mock_sensors_angle(180.0);
	motion_lid_calc();
	set_mock_sensors_angle(350.0);
	for (int i = 0; i <= TABLET_MODE_DEBOUNCE_COUNT + 1; i++) {
		motion_lid_calc();
	}
	zassert_within(motion_lid_get_angle(), 350, 2);
	zassert_equal(tablet_get_mode(), 1);

	/* 10. Close lid with 10 deg: small angle is valid when closed */
	emul_lid_close();
	k_sleep(K_MSEC(50));
	set_mock_sensors_angle(10.0);
	for (int i = 0; i <= TABLET_MODE_DEBOUNCE_COUNT + 1; i++) {
		motion_lid_calc();
	}
	zassert_within(motion_lid_get_angle(), 10, 2);
	zassert_equal(tablet_get_mode(), 0);
}

ZTEST(lid_angle, test_lid_angle_boundary_and_smoothing)
{
	int base_id = SENSOR_ID(DT_NODELABEL(base_accel));
	int lid_id = SENSOR_ID(DT_NODELABEL(lid_accel));

	emul_lid_open();
	k_sleep(K_MSEC(50));
	motion_sensors[base_id].current_range = 2;
	motion_sensors[lid_id].current_range = 2;

	/* 1. Axis acceleration overflow (> 110% 1G) */
	motion_sensors[base_id].xyz[X] = 0;
	motion_sensors[base_id].xyz[Y] = 0;
	motion_sensors[base_id].xyz[Z] = 20000;
	motion_sensors[lid_id].xyz[X] = 0;
	motion_sensors[lid_id].xyz[Y] = ONE_G_16BIT;
	motion_sensors[lid_id].xyz[Z] = 0;
	motion_lid_calc();
	zassert_equal(motion_lid_get_angle(), LID_ANGLE_UNRELIABLE);

	/* 2. Magnitude difference (> 1m/s^2 deviation) */
	motion_sensors[base_id].xyz[X] = 0;
	motion_sensors[base_id].xyz[Y] = 0;
	motion_sensors[base_id].xyz[Z] = 10000;
	motion_sensors[lid_id].xyz[X] = 0;
	motion_sensors[lid_id].xyz[Y] = ONE_G_16BIT;
	motion_sensors[lid_id].xyz[Z] = 0;
	motion_lid_calc();
	zassert_equal(motion_lid_get_angle(), LID_ANGLE_UNRELIABLE);

	/* 3. Large hinge vertical acceleration (> 8.7 m/s^2) */
	motion_sensors[base_id].xyz[X] = ONE_G_16BIT;
	motion_sensors[base_id].xyz[Y] = 0;
	motion_sensors[base_id].xyz[Z] = 0;
	motion_sensors[lid_id].xyz[X] = ONE_G_16BIT;
	motion_sensors[lid_id].xyz[Y] = 0;
	motion_sensors[lid_id].xyz[Z] = 0;
	motion_lid_calc();
	zassert_equal(motion_lid_get_angle(), LID_ANGLE_UNRELIABLE);

	/* 4. Hinge smoothing transition zone (between 7.0 and 8.7 m/s^2) */
	motion_sensors[base_id].xyz[X] = 12531;
	motion_sensors[base_id].xyz[Y] = 0;
	motion_sensors[base_id].xyz[Z] = 10540;
	motion_sensors[lid_id].xyz[X] = 12531;
	motion_sensors[lid_id].xyz[Y] = 10540;
	motion_sensors[lid_id].xyz[Z] = 0;
	motion_lid_calc();
	zassert_within(motion_lid_get_angle(), 90, 5);

	/* 5. 3-axis non-zero acceleration (180 deg) */
	motion_sensors[base_id].xyz[X] = 5296;
	motion_sensors[base_id].xyz[Y] = 7856;
	motion_sensors[base_id].xyz[Z] = 13712;
	motion_sensors[lid_id].xyz[X] = 5296;
	motion_sensors[lid_id].xyz[Y] = 7856;
	motion_sensors[lid_id].xyz[Z] = 13712;
	motion_lid_calc();
	zassert_within(motion_lid_get_angle(), 180, 2);
}

ZTEST(lid_angle, test_tablet_mode_threshold_and_host_cmd)
{
	struct ec_params_motion_sense params;
	struct ec_response_motion_sense response;
	struct host_cmd_handler_args args = {
		.params = &params,
		.params_size = sizeof(params),
		.response = &response,
		.response_max = sizeof(response),
		.version = 0,
	};

	/* 1. Test threshold validation via host_cmd_motion_lid */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_TABLET_MODE_LID_ANGLE;
	params.tablet_mode_threshold.lid_angle = EC_MOTION_SENSE_NO_VALUE;
	params.tablet_mode_threshold.hys_degree = EC_MOTION_SENSE_NO_VALUE;
	zassert_equal(host_cmd_motion_lid(&args), EC_RES_SUCCESS);

	params.tablet_mode_threshold.lid_angle = 180;
	params.tablet_mode_threshold.hys_degree = 20;
	zassert_equal(host_cmd_motion_lid(&args), EC_RES_SUCCESS);

	/* Negative values other than EC_MOTION_SENSE_NO_VALUE (-1) are invalid
	 */
	params.tablet_mode_threshold.lid_angle = -10;
	params.tablet_mode_threshold.hys_degree = 20;
	zassert_equal(host_cmd_motion_lid(&args), EC_RES_INVALID_PARAM);

	params.tablet_mode_threshold.lid_angle = 180;
	params.tablet_mode_threshold.hys_degree = -10;
	zassert_equal(host_cmd_motion_lid(&args), EC_RES_INVALID_PARAM);

	params.tablet_mode_threshold.lid_angle = 10;
	params.tablet_mode_threshold.hys_degree = 20;
	zassert_equal(host_cmd_motion_lid(&args), EC_RES_INVALID_PARAM);

	params.tablet_mode_threshold.lid_angle = 350;
	params.tablet_mode_threshold.hys_degree = 20;
	zassert_equal(host_cmd_motion_lid(&args), EC_RES_INVALID_PARAM);

	/* 2. Test host_cmd_motion_lid MOTIONSENSE_CMD_KB_WAKE_ANGLE */
	memset(&params, 0, sizeof(params));
	params.cmd = MOTIONSENSE_CMD_KB_WAKE_ANGLE;
	params.kb_wake_angle.data = 60;
	zassert_equal(host_cmd_motion_lid(&args), EC_RES_SUCCESS);
	zassert_equal(response.kb_wake_angle.ret, 60);
	zassert_equal(lid_angle_get_wake_angle(), 60);

	params.kb_wake_angle.data = EC_MOTION_SENSE_NO_VALUE;
	zassert_equal(host_cmd_motion_lid(&args), EC_RES_SUCCESS);
	zassert_equal(response.kb_wake_angle.ret, 60);

	/* 3. Test host_cmd_motion_lid MOTIONSENSE_CMD_LID_ANGLE */
	params.cmd = MOTIONSENSE_CMD_LID_ANGLE;
	zassert_equal(host_cmd_motion_lid(&args), EC_RES_SUCCESS);
	zassert_equal(response.lid_angle.value, motion_lid_get_angle());

	/* 4. Test host_cmd_motion_lid MOTIONSENSE_CMD_TABLET_MODE_LID_ANGLE */
	params.cmd = MOTIONSENSE_CMD_TABLET_MODE_LID_ANGLE;
	params.tablet_mode_threshold.lid_angle = 190;
	params.tablet_mode_threshold.hys_degree = 15;
	zassert_equal(host_cmd_motion_lid(&args), EC_RES_SUCCESS);
	zassert_equal(response.tablet_mode_threshold.lid_angle, 190);
	zassert_equal(response.tablet_mode_threshold.hys_degree, 15);

	/* 5. Test host_cmd_motion_lid invalid command */
	params.cmd = 0xff;
	zassert_equal(host_cmd_motion_lid(&args), EC_RES_INVALID_PARAM);
}

ZTEST(lid_angle, test_lid_angle_unreliable_in_tablet_mode)
{
	/* Open lid, rotate to 225 deg (tablet mode) */
	emul_lid_open();
	k_sleep(K_MSEC(50));
	set_mock_sensors_angle(225.0);
	for (int i = 0; i <= TABLET_MODE_DEBOUNCE_COUNT + 1; i++) {
		motion_lid_calc();
	}
	zassert_equal(tablet_get_mode(), 1);

	/* Reading an unreliable angle while in tablet mode resets debounce */
	set_mock_sensors_angle(10.0); /* unreliable small angle while open */
	motion_lid_calc();
	zassert_equal(motion_lid_get_angle(), LID_ANGLE_UNRELIABLE);
	zassert_equal(tablet_get_mode(), 1);

	/* Rotate back to 90 deg: requires full debounce to return to clamshell
	 */
	set_mock_sensors_angle(90.0);
	motion_lid_calc();
	zassert_equal(tablet_get_mode(), 1);
	for (int i = 0; i <= TABLET_MODE_DEBOUNCE_COUNT + 1; i++) {
		motion_lid_calc();
	}
	zassert_equal(tablet_get_mode(), 0);
}

#endif
