/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * LIS2DW12 accelerometer include file for Chrome EC 3D digital accelerometer.
 * For more details on LIS2DW12 device please refer to www.st.com.
 */

#ifndef PLATFORM_EC_DRIVER_ACCEL_LIS2DW12_H_
#define PLATFORM_EC_DRIVER_ACCEL_LIS2DW12_H_

#include "driver/accel_lis2dw12_public.h"
#include "stm_mems_common.h"

/* Who am I. */
#define LIS2DW12_WHO_AM_I_REG 0x0f
#define LIS2DW12_WHO_AM_I 0x44

/* Registers sensor. */
#define LIS2DW12_CTRL1_ADDR 0x20
#define LIS2DW12_CTRL2_ADDR 0x21
#define LIS2DW12_CTRL3_ADDR 0x22

#define LIS2DW12_CTRL4_ADDR 0x23

/* CTRL4 bits. */
#define LIS2DW12_INT1_FTH 0x02
#define LIS2DW12_INT1_D_TAP 0x08
#define LIS2DW12_INT1_S_TAP 0x40

#define LIS2DW12_CTRL5_ADDR 0x24

/* CTRL5 bits. */
#define LIS2DW12_INT2_FTH 0x02

#define LIS2DW12_CTRL6_ADDR 0x25
#define LIS2DW12_STATUS_REG 0x27

/* STATUS bits. */
#define LIS2DW12_STS_DRDY_UP 0x01
#define LIS2DW12_SINGLE_TAP_UP 0x08
#define LIS2DW12_DOUBLE_TAP_UP 0x10
#define LIS2DW12_FIFO_THS_UP 0x80

#define LIS2DW12_OUT_X_L_ADDR 0x28
#define LIS2DW12_OUT_X_H_ADDR 0x29
#define LIS2DW12_OUT_Y_L_ADDR 0x2a
#define LIS2DW12_OUT_Y_H_ADDR 0x2b
#define LIS2DW12_OUT_Z_L_ADDR 0x2c
#define LIS2DW12_OUT_Z_H_ADDR 0x2d

#define LIS2DW12_FIFO_CTRL_ADDR 0x2e

/* FIFO_CTRL bits. */
#define LIS2DW12_FIFO_MODE_MASK 0xe0

/* List of supported FIFO mode. */
enum lis2dw12_fmode {
	LIS2DW12_FIFO_BYPASS_MODE = 0,
	LIS2DW12_FIFO_MODE,
	LIS2DW12_FIFO_CONT_MODE = 6
};

#define LIS2DW12_FIFO_THRESHOLD_MASK 0x1f

#define LIS2DW12_FIFO_SAMPLES_ADDR 0x2f
#define LIS2DW12_TAP_THS_X_ADDR 0x30
#define LIS2DW12_TAP_THS_Y_ADDR 0x31
#define LIS2DW12_TAP_THS_Z_ADDR 0x32
#define LIS2DW12_TAP_X_EN 0x80
#define LIS2DW12_TAP_Y_EN 0x40
#define LIS2DW12_TAP_Z_EN 0x20
#define LIS2DW12_TAP_EN_ALL \
	(LIS2DW12_TAP_X_EN | LIS2DW12_TAP_Y_EN | LIS2DW12_TAP_Z_EN)
#define LIS2DW12_TAP_THS_MASK 0x1f
/*
 * Tap threshold: 1 LSB of TAP_THS_* is FS / 32 (in g), and 0 disables
 * tap detection.
 */
#ifdef CONFIG_GESTURE_SENSOR_DOUBLE_TAP
#define LIS2DW12_TAP_THS_REG(_fs)                                   \
	MIN(MAX(DIV_ROUND_NEAREST(CONFIG_GESTURE_TAP_THRES_MG * 32, \
				  (_fs) * 1000),                    \
		1),                                                 \
	    LIS2DW12_TAP_THS_MASK)
#else
#define LIS2DW12_TAP_THS_REG(_fs) 0
#endif

#define LIS2DW12_INT_DUR_ADDR 0x33
#define LIS2DW12_TAP_LATENCY_MASK 0xf0
/*
 * INT_DUR fields at ODR = 400 Hz:
 * - SHOCK[1:0]   (bits 1:0): 1 LSB = 8 / ODR (3 -> 60 ms)
 * - QUIET[1:0]   (bits 3:2): 1 LSB = 4 / ODR (3 -> 30 ms)
 * - LATENCY[3:0] (bits 7:4): 1 LSB = 32 / ODR (6 -> 480 ms for 500 ms window)
 */
#define LIS2DW12_TAP_SHOCK_DEFAULT 0x03
#define LIS2DW12_TAP_QUIET_DEFAULT (0x03 << 2)
#define LIS2DW12_TAP_LATENCY_REG(_ms) \
	(MIN(((_ms) * 400) / (32 * 1000), LIS2DW12_TAP_LATENCY_MASK >> 4) << 4)
#ifdef CONFIG_GESTURE_SENSOR_DOUBLE_TAP
#define LIS2DW12_INT_DUR_VAL                                             \
	(LIS2DW12_TAP_LATENCY_REG(CONFIG_GESTURE_TAP_MAX_INTERSTICE_T) | \
	 LIS2DW12_TAP_QUIET_DEFAULT | LIS2DW12_TAP_SHOCK_DEFAULT)
#else
#define LIS2DW12_INT_DUR_VAL 0
#endif

#define LIS2DW12_WAKE_UP_THS_ADDR 0x34

/* TAP bits. */
#define LIS2DW12_SINGLE_DOUBLE_TAP 0x80

#define LIS2DW12_TAP_SRC_ADDR 0x39
#define LIS2DW12_TAP_SRC_DOUBLE_TAP 0x10
#define LIS2DW12_TAP_SRC_SINGLE_TAP 0x20
#define LIS2DW12_TAP_SRC_TAP_IA 0x40

/* FIFO_SAMPLES bits. */
#define LIS2DW12_FIFO_DIFF_MASK 0x3f
#define LIS2DW12_FIFO_OVR_MASK 0x40
#define LIS2DW12_FIFO_FTH_MASK 0x80

#define LIS2DW12_ABS_INT_CFG_ADDR 0x3f

/* INT Configuration bits. */
#define LIS2DW12_DRDY_PULSED 0x80
#define LIS2DW12_INT2_ON_INT1 0x40
#define LIS2DW12_INT_ENABLE 0x20

/* Alias Registers/Masks. */
#define LIS2DW12_ACC_ODR_ADDR LIS2DW12_CTRL1_ADDR
#define LIS2DW12_ACC_ODR_MASK 0xf0

#define LIS2DW12_ACC_MODE_ADDR LIS2DW12_CTRL1_ADDR
#define LIS2DW12_ACC_MODE_MASK 0x0c

/* Power mode selection. */
enum lis2sw12_mode {
	LIS2DW12_LOW_POWER = 0,
	LIS2DW12_HIGH_PERF,
	LIS2DW12_SINGLE_DC,
	LIS2DW12_LOW_POWER_LIST_NUM
};

#define LIS2DW12_ACC_LPMODE_ADDR LIS2DW12_CTRL1_ADDR
#define LIS2DW12_ACC_LPMODE_MASK 0x03

/*
 * Low power mode selection.
 * TODO: Support all Low Power Mode. Actually is not supported only
 * LOW_POWER_MODE_1.
 */
enum lis2sw12_lpmode {
	LIS2DW12_LOW_POWER_MODE_1 = 0,
	LIS2DW12_LOW_POWER_MODE_2,
	LIS2DW12_LOW_POWER_MODE_3,
	LIS2DW12_LOW_POWER_MODE_4,
	LIS2DW12_LOW_POWER_MODE_LIST_NUM
};

#define LIS2DW12_BDU_ADDR LIS2DW12_CTRL2_ADDR
#define LIS2DW12_BDU_MASK 0x08

#define LIS2DW12_SOFT_RESET_ADDR LIS2DW12_CTRL2_ADDR
#define LIS2DW12_SOFT_RESET_MASK 0x40

#define LIS2DW12_BOOT_ADDR LIS2DW12_CTRL2_ADDR
#define LIS2DW12_BOOT_MASK 0x80

#define LIS2DW12_LIR_ADDR LIS2DW12_CTRL3_ADDR
#define LIS2DW12_LIR_MASK 0x10

#define LIS2DW12_H_ACTIVE_ADDR LIS2DW12_CTRL3_ADDR
#define LIS2DW12_H_ACTIVE_MASK 0x08

#define LIS2DW12_INT1_FTH_ADDR LIS2DW12_CTRL4_ADDR
#define LIS2DW12_INT1_FTH_MASK LIS2DW12_INT1_FTH

#define LIS2DW12_INT1_TAP_ADDR LIS2DW12_CTRL4_ADDR
#define LIS2DW12_INT1_DTAP_MASK 0x08
#define LIS2DW12_INT1_STAP_MASK 0x40

#define LIS2DW12_INT1_D_TAP_EN LIS2DW12_INT1_DTAP_MASK

#define LIS2DW12_STATUS_TAP LIS2DW12_TAP_SRC_ADDR
#define LIS2DW12_SINGLE_TAP LIS2DW12_TAP_SRC_SINGLE_TAP
#define LIS2DW12_DOUBLE_TAP LIS2DW12_TAP_SRC_DOUBLE_TAP

#define LIS2DW12_INT2_ON_INT1_ADDR LIS2DW12_ABS_INT_CFG_ADDR
#define LIS2DW12_INT2_ON_INT1_MASK LIS2DW12_INT2_ON_INT1

#define LIS2DW12_DRDY_PULSED_ADDR LIS2DW12_ABS_INT_CFG_ADDR
#define LIS2DW12_DRDY_PULSED_MASK LIS2DW12_DRDY_PULSED

/* Acc data rate for HR mode. */
enum lis2dw12_odr {
	LIS2DW12_ODR_POWER_OFF_VAL = 0x00,
	LIS2DW12_ODR_12HZ_VAL = 0x02,
	LIS2DW12_ODR_25HZ_VAL,
	LIS2DW12_ODR_50HZ_VAL,
	LIS2DW12_ODR_100HZ_VAL,
	LIS2DW12_ODR_200HZ_VAL,
	LIS2DW12_ODR_400HZ_VAL,
	LIS2DW12_ODR_800HZ_VAL,
	LIS2DW12_ODR_1_6kHZ_VAL,
	LIS2DW12_ODR_LIST_NUM
};

/* Full scale range registers. */
#define LIS2DW12_FS_ADDR LIS2DW12_CTRL6_ADDR
#define LIS2DW12_FS_MASK 0x30

/* Acc FS value. */
enum lis2dw12_fs {
	LIS2DW12_FS_2G_VAL = 0x00,
	LIS2DW12_FS_4G_VAL,
	LIS2DW12_FS_8G_VAL,
	LIS2DW12_FS_16G_VAL,
	LIS2DW12_FS_LIST_NUM
};

#define LIS2DW12_ACCEL_FS_MIN_VAL 2
#define LIS2DW12_ACCEL_FS_MAX_VAL 16

#define LIS2DW12_LOW_NOISE_ADDR LIS2DW12_CTRL6_ADDR
#define LIS2DW12_LOW_NOISE_MASK 0x04

/* Acc Gain value. */
#define LIS2DW12_FS_2G_GAIN 3904
#define LIS2DW12_FS_4G_GAIN (LIS2DW12_FS_2G_GAIN << 1)
#define LIS2DW12_FS_8G_GAIN (LIS2DW12_FS_2G_GAIN << 2)
#define LIS2DW12_FS_16G_GAIN (LIS2DW12_FS_2G_GAIN << 3)

/* FS Full Scale value from Gain. */
#define LIS2DW12_GAIN_FS(_gain) \
	(2 << (31 - __builtin_clz(_gain / LIS2DW12_FS_2G_GAIN)))

/* Gain value from selected Full Scale. */
#define LIS2DW12_FS_GAIN(_fs) (LIS2DW12_FS_2G_GAIN << (30 - __builtin_clz(_fs)))

/* Reg value from Full Scale. */
#define LIS2DW12_FS_REG(_fs) (30 - __builtin_clz(_fs))

/* Normalized FS value from Full Scale. */
#define LIS2DW12_NORMALIZE_FS(_fs) (1 << (31 - __builtin_clz(_fs)))

/*
 * Sensor resolution in number of bits.
 * Sensor driver support 14 bits resolution.
 * TODO: Support all "LP Power Mode" (res. 12/14 bits).
 */
#define LIS2DW12_RESOLUTION 14

/** Maximum possible sample */
#define LIS2DW12_SAMPLE_MAX ((1 << (LIS2DW12_RESOLUTION - 1)) - 1)

/** Smallest possible sample */
#define LIS2DW12_SAMPLE_MIN (-(1 << (LIS2DW12_RESOLUTION - 1)))

#ifdef CONFIG_ZTEST
int lis2dw12_set_power_mode(const struct motion_sensor_t *s,
			    enum lis2sw12_mode mode,
			    enum lis2sw12_lpmode lpmode);
#endif

#if DT_NODE_EXISTS(DT_ALIAS(lis2dw12_int))
/* Get the motion sensor ID of the LIS2DW12 sensor that generates the
 * interrupt. The interrupt is converted to the event and transferred to
 * motion sense task that actually handles the interrupt.
 *
 * Here we use an alias (lis2dw12_int) to get the motion sensor ID. This alias
 * MUST be defined for this driver to work.
 * aliases {
 *   lis2dw12-int = &lid_accel;
 * };
 */
#define CONFIG_ACCEL_LIS2DW12_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(SENSOR_ID(DT_ALIAS(lis2dw12_int)))
#endif

#endif /* PLATFORM_EC_DRIVER_ACCEL_LIS2DW12_H_ */
