/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Rohm BH1730 Ambient light sensor driver
 */

#include "driver/als_bh1730.h"
#include "i2c.h"
#include "console.h"
#include "math_util.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_SENSE, format, ## args)

#ifdef CONFIG_CMD_BH1730_TEST
/* Test data from console command */
uint16_t test_data_use = 0;
uint16_t test_data0 = 0;
uint16_t test_data1 = 0;
#endif

/* Sensor configuration */
/* Select Gain */
#define BH1730_CONF_GAIN BH1730_GAIN_GAIN_X1_GAIN
/* Select Itime */
#define BH1730_CONF_ITIME 0xDA
/* ALS CONSTANT will be changed according to project */
#ifndef CONFIG_ALS_BH1730_CONSTANTS
#define LUXTH1_1K		260
#define LUXTH1_D0_1K		1290
#define LUXTH1_D1_1K		2733
#define LUXTH2_1K		550
#define LUXTH2_D0_1K		797
#define LUXTH2_D1_1K		859
#define LUXTH3_1K		1090
#define LUXTH3_D0_1K		510
#define LUXTH3_D1_1K		345
#define LUXTH4_1K		2130
#define LUXTH4_D0_1K		276
#define LUXTH4_D1_1K		130
#endif

static uint32_t itime_ms_x_10 = (256 - BH1730_CONF_ITIME) * 27;

/**
 * Convert BH1730 data0, data1 to lux
 */
static int bh1730_convert_to_lux(uint16_t data0, uint16_t data1)
{
	int lux;

	if ( data0 == 0 ) {
		lux = 2;
		return lux;
	}

	{
	uint32_t d0_1k = data0 * 1000;
	uint32_t d1_1k = data1 * 1000;
	uint32_t d_temp = d1_1k / d0_1k;
	uint32_t d_lux;
	uint32_t itime_ms_1k = itime_ms_x_10 * 100;

	if(d_temp < LUXTH1_1K) {
		d0_1k = LUXTH1_D0_1K * data0;
		d1_1k = LUXTH1_D1_1K * data1;
	}
	else if(d_temp < LUXTH2_1K) {
		d0_1k = LUXTH2_D0_1K * data0;
		d1_1k = LUXTH2_D1_1K * data1;
	}
	else if(d_temp < LUXTH3_1K) {
		d0_1k = LUXTH3_D0_1K * data0;
		d1_1k = LUXTH3_D1_1K * data1;
	}
	else if(d_temp < LUXTH4_1K) {
		d0_1k = LUXTH4_D0_1K * data0;
		d1_1k = LUXTH4_D1_1K * data1;
	}
	else {
		return 2;
	}

	d_lux = d0_1k - d1_1k;
	d_lux *= 100;
	lux = d_lux / itime_ms_1k;
	}

	return lux;
}

/**
 * Read BH1730 data0 and data1
 */
static int bh1730_read_data(int port, int addr, uint16_t *data0, uint16_t *data1)
{

	int ret;
	int data;

#ifdef CONFIG_CMD_BH1730_TEST
	if (test_data_use) {
		*data0 = test_data0;
		*data1 = test_data1;
		test_data_use = 0;
		return EC_SUCCESS;
	}
#endif

	ret = i2c_read32(port, addr,
			 BH1730_DATA0LOW, &data);

	if (ret != EC_SUCCESS)
		return ret;

	*data0 = 0x0000ffff & data;
	*data1 = (data >> 16) & 0x0000ffff;

	return EC_SUCCESS;
}

/**
 * Initialise BH1730 Ambient light sensor.
 */
static int bh1730_init_sensor(int port, int addr)
{
	int ret;

	CPRINTF("bh1730_init_sensor \n");

	/* power and measurement bit high */
	ret = i2c_write8(port, addr,
			 BH1730_CONTROL,
			 BH1730_CONTROL_POWER_ENABLE|BH1730_CONTROL_ADC_EN_ENABLE);

	if (ret != EC_SUCCESS) {
		CPRINTF("bh1730_init_sensor - enable fail %d\n", ret);
		return ret;
	}

	/* set timing */
	ret = i2c_write8(port, addr,
			 BH1730_TIMING, BH1730_CONF_ITIME);

	if (ret != EC_SUCCESS) {
		CPRINTF("bh1730_init_sensor - time fail %d\n", ret);
		return ret;
	}

	/* set ADC gain */
	ret = i2c_write8(port, addr,
			 BH1730_GAIN, BH1730_CONF_GAIN);

	if (ret != EC_SUCCESS) {
		CPRINTF("bh1730_init_sensor - gain fail %d\n", ret);
		return ret;
	}

	CPRINTF("bh1730_init_sensor ok\n");

	return EC_SUCCESS;
}

#ifdef HAS_TASK_ALS
/**
 * Initialise BH1730 Ambient light sensor.
 */
int bh1730_init(void)
{
	int ret;

	CPRINTF("bh1730_init \n");

	ret = bh1730_init_sensor(I2C_PORT_ALS,BH1730_I2C_ADDR);

	if (ret != EC_SUCCESS) {
		CPRINTF("bh1730_init_sensor - fail %d\n", ret);
		return ret;
	}

	CPRINTF("bh1730_init ok\n");

	return EC_SUCCESS;
}

/**
 * Read BH1730 Ambient light sensor data.
 */
int bh1730_read_lux(int *lux, int af)
{
	int ret;
	uint16_t data0;
	uint16_t data1;

	/* read data0 and data1 from sensor */
	ret = bh1730_read_data(I2C_PORT_ALS, BH1730_I2C_ADDR, &data0, &data1);
	if (ret != EC_SUCCESS) {
		CPRINTF("bh1730_read_lux - fail %d\n", ret);
		return ret;
	}

	/* convert sensor data0 and data1 to lux */
	*lux = bh1730_convert_to_lux(data0, data1);

	CPRINTF("bh1730_read_lux d0=%d, d1=%d, lux=%d\n", data0, data1, *lux);

	*lux = *lux * af;

	return EC_SUCCESS;
}

#ifdef CONFIG_CMD_BH1730_TEST
/* BH1730 console test command */
/* "bh1730 init" = initialize sensor */
/* "bh1730" = read data from sensor and convert to lux */
/* "bh1730 539 82" = use command line test data and convert to lux */
static int command_bh1730_test(int argc, char **argv)
{
	int rv, lux, af;

	/* init sensor */
	if (argc == 2 && !strcasecmp(argv[1], "init")) {
		rv = bh1730_init();
		CPRINTF("command_bh1730_test - rv %d, init\n", rv);
		return EC_SUCCESS;
	}

	/* use data0 and data1 from command line */
	if (argc == 3) {
		char *e;

		/* use test data0 and data1 from command line */
		test_data_use = 1;
		test_data0 = strtoi(argv[1], &e, 0);
		test_data1 = strtoi(argv[2], &e, 0);
	}

	af = 1;
	rv = bh1730_read_lux(&lux, af);

	CPRINTF("command_bh1730_test - rv %d, lux = %d\n", rv, lux);

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(bh1730, command_bh1730_test,
			NULL,
			"BH1730 test", "als test");
#endif
#else    /* HAS_TASK_ALS */

#include "accelgyro.h"

/**
 * Read BH1730 light sensor data.
 */
int bh1730_read_lux(const struct motion_sensor_t *s, vector_3_t v)
{
	struct bh1730_drv_data_t *drv_data = BH1730_GET_DATA(s);
	int ret;
	int lux;
	uint16_t data0;
	uint16_t data1;

	/* read data0 and data1 from sensor */
	ret = bh1730_read_data(s->port, s->addr, &data0, &data1);
	if (ret != EC_SUCCESS) {
		CPRINTF("bh1730_read_lux - fail %d\n", ret);
		return ret;
	}

	/* convert sensor data0 and data1 to lux */
	lux = bh1730_convert_to_lux(data0, data1);

	v[0] = lux;
	v[1] = 0;
	v[2] = 0;

	/*
	 * Return an error when nothing change to prevent filling the
	 * fifo with useless data.
	 */
	if (v[0] == drv_data->last_value)
		return EC_ERROR_UNCHANGED;
	else
		return EC_SUCCESS;
}

static int bh1730_set_range(const struct motion_sensor_t *s, int range,
			     int rnd)
{
	return EC_SUCCESS;
}

static int bh1730_get_range(const struct motion_sensor_t *s)
{
	return EC_SUCCESS;
}


static int bh1730_set_data_rate(const struct motion_sensor_t *s,
				int rate, int roundup)
{
	struct bh1730_drv_data_t *drv_data = BH1730_GET_DATA(s);
	int ret, itime, irate;

	CPRINTF("RATE : %d\n", rate);

	if(rate < 1) {
		drv_data->rate = 100000;
		return EC_SUCCESS;
	}

	irate = 1000000 / rate;
	itime = 256 - 10000 / (irate * 27);

	ret = i2c_write8(s->port, s->addr,
			 BH1730_TIMING, itime);

	if (ret != EC_SUCCESS) {
		return ret;
	}

	itime_ms_x_10 = (256 - itime) * 27;

	drv_data->rate = rate;

	CPRINTF("ITIME : %d\n", itime);

	return EC_SUCCESS;
}

static int bh1730_get_data_rate(const struct motion_sensor_t *s)
{
	struct bh1730_drv_data_t *drv_data = BH1730_GET_DATA(s);

	return drv_data->rate;
}

static int bh1730_set_offset(const struct motion_sensor_t *s,
			const int16_t *offset,
			int16_t    temp)
{
	return EC_SUCCESS;
}

static int bh1730_get_offset(const struct motion_sensor_t *s,
			int16_t   *offset,
			int16_t    *temp)
{
	return EC_SUCCESS;
}

/**
 * Initialise BH1730 Ambient light sensor.
 */
int bh1730_init(const struct motion_sensor_t *s)
{
	int ret;

	CPRINTF("bh1730_init \n");

	ret = bh1730_init_sensor(s->port, s->addr);

	if (ret != EC_SUCCESS) {
		CPRINTF("bh1730_init_sensor - fail %d\n", ret);
		return ret;
	}

	CPRINTF("bh1730_init ok\n");

	return EC_SUCCESS;
}

const struct accelgyro_drv bh1730_drv = {
	.init = bh1730_init,
	.read = bh1730_read_lux,
	.set_range = bh1730_set_range,
	.get_range = bh1730_get_range,
	.set_offset = bh1730_set_offset,
	.get_offset = bh1730_get_offset,
	.set_data_rate = bh1730_set_data_rate,
	.get_data_rate = bh1730_get_data_rate,
};
#endif   /* HAS_TASK_ALS */

