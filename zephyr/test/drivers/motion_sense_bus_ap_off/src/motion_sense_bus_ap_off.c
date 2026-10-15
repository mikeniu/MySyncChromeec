/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "chipset.h"
#include "hooks.h"
#include "motion_sense.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

extern enum chipset_state_mask sensor_active;
void motion_sense_startup(void);

FAKE_VALUE_FUNC(int, mock_init, struct motion_sensor_t *);
/* intv3_t decays to int * in a parameter list */
FAKE_VALUE_FUNC(int, mock_read, const struct motion_sensor_t *, int *);
FAKE_VALUE_FUNC(int, mock_set_range, struct motion_sensor_t *, int, int);
FAKE_VALUE_FUNC(int, mock_set_data_rate, const struct motion_sensor_t *, int,
		int);
FAKE_VALUE_FUNC(int, mock_get_data_rate, const struct motion_sensor_t *);

/* Minimal driver so the test controls when init succeeds. */
static const struct accelgyro_drv mock_drv = {
	.init = mock_init,
	.read = mock_read,
	.set_range = mock_set_range,
	.set_data_rate = mock_set_data_rate,
	.get_data_rate = mock_get_data_rate,
};

/* Sensor only active with the AP off, like the tap sensor on Quartz. */
static struct motion_sensor_t *ap_off_sensor = &motion_sensors[0];
/* Sensor used with the AP up, must not be touched by the AP-off logic. */
static struct motion_sensor_t *ap_on_sensor = &motion_sensors[1];

static const struct accelgyro_drv *orig_drv;
static enum chipset_state_mask orig_ap_off_mask;
static enum chipset_state_mask orig_ap_on_mask;

static void bus_ap_off_before(void *state)
{
	ARG_UNUSED(state);

	RESET_FAKE(mock_init);
	RESET_FAKE(mock_read);
	RESET_FAKE(mock_set_range);
	RESET_FAKE(mock_set_data_rate);
	RESET_FAKE(mock_get_data_rate);

	orig_drv = ap_off_sensor->drv;
	orig_ap_off_mask = ap_off_sensor->active_mask;
	orig_ap_on_mask = ap_on_sensor->active_mask;

	ap_off_sensor->drv = &mock_drv;
	ap_off_sensor->active_mask = SENSOR_ACTIVE_S5;
	ap_off_sensor->state = SENSOR_NOT_INITIALIZED;
	ap_off_sensor->collection_rate = 0;
	ap_on_sensor->active_mask = SENSOR_ACTIVE_S0_S3;
}

static void bus_ap_off_after(void *state)
{
	ARG_UNUSED(state);

	ap_off_sensor->drv = orig_drv;
	ap_off_sensor->active_mask = orig_ap_off_mask;
	ap_off_sensor->state = SENSOR_NOT_INITIALIZED;
	ap_on_sensor->active_mask = orig_ap_on_mask;
	sensor_active = SENSOR_ACTIVE_S0;
}

ZTEST(motion_sense_bus_ap_off, test_init_waits_for_shutdown_complete)
{
	/* Settle in S0 first so the power sequencing does not race the test */
	test_set_chipset_to_s0();
	RESET_FAKE(mock_init);
	ap_off_sensor->state = SENSOR_NOT_INITIALIZED;

	/* The bus still belongs to the AP, so init must not be called. */
	mock_init_fake.return_val = EC_ERROR_BUSY;
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	k_sleep(K_MSEC(100));

	zassert_equal(sensor_active, SENSOR_ACTIVE_S5);
	zassert_equal(mock_init_fake.call_count, 0);
	zassert_equal(ap_off_sensor->state, SENSOR_NOT_INITIALIZED);

	/* The bus is ours again */
	mock_init_fake.return_val = EC_SUCCESS;
	hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);
	k_sleep(K_MSEC(100));

	zassert_equal(mock_init_fake.call_count, 1);
	zassert_true(ap_off_sensor->state == SENSOR_INITIALIZED ||
		     ap_off_sensor->state == SENSOR_READY);
}

ZTEST(motion_sense_bus_ap_off, test_startup_stops_ap_off_sensors)
{
	test_set_chipset_to_s0();

	sensor_active = SENSOR_ACTIVE_S5;
	ap_off_sensor->state = SENSOR_READY;
	ap_off_sensor->collection_rate = 1000;
	ap_on_sensor->state = SENSOR_READY;
	ap_on_sensor->collection_rate = 1000;

	hook_notify(HOOK_CHIPSET_STARTUP);

	zassert_equal(sensor_active, SENSOR_ACTIVE_S3);
	zassert_equal(ap_off_sensor->state, SENSOR_NOT_INITIALIZED);
	zassert_equal(ap_off_sensor->collection_rate, 0);
	/* Sensors used with the AP up are left to the resume path */
	zassert_equal(ap_on_sensor->state, SENSOR_READY);
	zassert_equal(ap_on_sensor->collection_rate, 1000);
}

ZTEST(motion_sense_bus_ap_off, test_boot_with_ap_off)
{
	/* Move the chipset to G3 and reset the mock state before testing HOOK_INIT. */
	test_set_chipset_to_g3();
	RESET_FAKE(mock_init);
	ap_off_sensor->state = SENSOR_NOT_INITIALIZED;

	/* When booting with the AP off, HOOK_INIT should initialize the sensor. */
	mock_init_fake.return_val = EC_SUCCESS;
	motion_sense_startup();
	k_sleep(K_MSEC(100));

	zassert_equal(sensor_active, SENSOR_ACTIVE_S5);
	zassert_equal(mock_init_fake.call_count, 1);
	zassert_true(ap_off_sensor->state == SENSOR_INITIALIZED ||
		     ap_off_sensor->state == SENSOR_READY);
}

ZTEST(motion_sense_bus_ap_off, test_boot_with_ap_on)
{
	/* Simulate an EC reboot while the AP is in S0. */
	test_set_chipset_to_s0();
	RESET_FAKE(mock_init);
	ap_off_sensor->state = SENSOR_NOT_INITIALIZED;

	/* The AP-off sensor must not be initialized while the AP is on. */
	mock_init_fake.return_val = EC_ERROR_BUSY;
	motion_sense_startup();
	k_sleep(K_MSEC(100));

	zassert_equal(sensor_active, SENSOR_ACTIVE_S0);
	zassert_equal(mock_init_fake.call_count, 0);
	zassert_equal(ap_off_sensor->state, SENSOR_NOT_INITIALIZED);
}

ZTEST_SUITE(motion_sense_bus_ap_off, drivers_predicate_post_main, NULL,
	    bus_ap_off_before, bus_ap_off_after, NULL);
