/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accel_lis2dh.h"
#include "motion_sense.h"
#include "motion_sense_fifo.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_lis2dh.h"
#include "test/drivers/test_state.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/ztest.h>

#define LIS2DH_NODE DT_NODELABEL(lis2dh_emul)
#define ACC_SENSOR_ID SENSOR_ID(DT_NODELABEL(ms_lis2dh_accel))


static const struct emul *emul = EMUL_DT_GET(LIS2DH_NODE);
static struct motion_sensor_t *acc = &motion_sensors[ACC_SENSOR_ID];

ZTEST_SUITE(lis2dh, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

static int read_wrong_chip_id(const struct emul *target, int reg, uint8_t *val,
			      int bytes, void *data)
{
	if (reg == LIS2DH_WHO_AM_I_REG) {
		*val = 0x13;
		return 0;
	}
	return 1;
}

ZTEST_USER(lis2dh, test_init)
{
	/* Basic initialization works. */
	zassert_ok(lis2dh_drv.init(acc));

	/* Unexpected chip ID is an error. */
	i2c_common_emul_set_read_func(lis2dh_emul_get_i2c(emul),
				      read_wrong_chip_id, NULL);
	zassert_equal(EC_ERROR_HW_INTERNAL, lis2dh_drv.init(acc));
}
