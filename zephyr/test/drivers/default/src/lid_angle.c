/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "lid_angle.h"
#include "motion_lid.h"
#include "motion_sense.h"
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

	/* Large-to-small: initial angle 340 deg, noisy reading 25 deg */
	set_mock_sensors_angle(340.0);
	motion_lid_calc();
	zassert_within(motion_lid_get_angle(), 340, 2,
		       "Initial angle should be ~340, got %d",
		       motion_lid_get_angle());

	/* With symmetric debounce, it should correct 25 to 335 (360 - 25) */
	set_mock_sensors_angle(25.0);
	motion_lid_calc();
	zassert_within(motion_lid_get_angle(), 335, 2,
		       "Noisy 25 should be debounced to ~335, got %d",
		       motion_lid_get_angle());
}

#endif
