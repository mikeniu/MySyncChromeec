/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * CAPELLA VCNL4200 light and proximity sensor driver
 */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/als_vcnl4200.h"
#include "i2c.h"
#include "math_util.h"

#define CPRINTS(format, args...) cprints(CC_HOOK, format, ##args)

#define VCNL4200_GET_DATA(_s) ((struct vcnl4200_drv_data *)(_s)->drv_data)

/*
 * Read data from VCNL4200 proximity sensor.
 */
static int vcnl4200_read(const struct motion_sensor_t *s, intv3_t v)
{
	struct vcnl4200_drv_data *drv_data = VCNL4200_GET_DATA(s);

	int ps_data;
	int ret;

	ret = i2c_read16(s->port, s->i2c_spi_addr_flags,
			VCNL4200_PS_DATA, &ps_data);

	if (ret)
		return ret;

	v[0] = ps_data;
	v[1] = 0;
	v[2] = 0;

	/*
	 * Return an error when nothing change to prevent filling the
	 * fifo with useless data.
	 */
	if (v[0] == drv_data->last_value)
		return EC_ERROR_UNCHANGED;

	drv_data->last_value = v[0];
	return EC_SUCCESS;
}

static int vcnl4200_set_range(struct motion_sensor_t *s, int range, int rnd)
{
	return EC_SUCCESS;
}

static int vcnl4200_set_data_rate(const struct motion_sensor_t *s, int rate,
				  int roundup)
{
	VCNL4200_GET_DATA(s)->rate = rate;
	return EC_SUCCESS;
}

static int vcnl4200_get_data_rate(const struct motion_sensor_t *s)
{
	return VCNL4200_GET_DATA(s)->rate;
}

static int vcnl4200_set_offset(const struct motion_sensor_t *s,
			       const int16_t *offset, int16_t temp)
{
	/* TODO: check calibration method */
	return EC_SUCCESS;
}

static int vcnl4200_get_offset(const struct motion_sensor_t *s, int16_t *offset,
			       int16_t *temp)
{
	*offset = VCNL4200_GET_DATA(s)->offset;
	return EC_SUCCESS;
}

#ifdef CONFIG_ACCEL_INTERRUPTS
static int vcnl4200_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	return vcnl4200_read(s, s->xyz);
}
#endif

/**
 * Initialise VCNL4200 light sensor.
 */
static int vcnl4200_init(struct motion_sensor_t *s)
{
	int ret;

	ret = i2c_write16(s->port, s->i2c_spi_addr_flags, VCNL4200_PS_CONF1,
			  VCNL4200_PS_SD);

	if (ret)
		return ret;

	ret = i2c_write16(s->port, s->i2c_spi_addr_flags, VCNL4200_PS_CONF1,
			  VCNL4200_PS_CONF1_DEFAULT);

	if (ret)
		return ret;

	ret = i2c_write16(s->port, s->i2c_spi_addr_flags, VCNL4200_PS_CONF3,
			  VCNL4200_PS_CONF3_DEFAULT);

	if (ret)
		return ret;

	ret = i2c_write16(s->port, s->i2c_spi_addr_flags, VCNL4200_PS_CANC, 0);

	if (ret)
		return ret;

	return sensor_init_done(s);
}

const struct accelgyro_drv vcnl4200_drv = {
	.init = vcnl4200_init,
	.read = vcnl4200_read,
	.set_range = vcnl4200_set_range,
	.set_offset = vcnl4200_set_offset,
	.get_offset = vcnl4200_get_offset,
	.set_data_rate = vcnl4200_set_data_rate,
	.get_data_rate = vcnl4200_get_data_rate,
#ifdef CONFIG_ACCEL_INTERRUPTS
	.irq_handler = vcnl4200_irq_handler,
#endif
};
