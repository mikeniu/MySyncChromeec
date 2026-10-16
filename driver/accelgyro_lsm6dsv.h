/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LSM6DSV / LSM6DSVQ Accel and Gyro driver for Chrome EC */

#ifndef PLATFORM_EC_DRIVER_ACCELGYRO_LSM6DSV_H_
#define PLATFORM_EC_DRIVER_ACCELGYRO_LSM6DSV_H_

#include "driver/accelgyro_lsm6dsv_public.h"
#include "stm_mems_common.h"

/* Common defines for Acc and Gyro sensors */
#define LSM6DSV_EN_BIT 0x01
#define LSM6DSV_DIS_BIT 0x00

/*
 * Register bank selection and global reset. All the other registers,
 * WHO_AM_I included, differ between the register banks.
 */
#define LSM6DSV_FUNC_CFG_ACCESS_ADDR 0x01
#define LSM6DSV_USER_BANK 0x00
#define LSM6DSV_SW_POR 0x04
/* Boot time after SW_POR, from AN5763 */
#define LSM6DSV_SW_POR_DELAY_MS 30

/* FIFO control registers */
#define LSM6DSV_FIFO_CTRL1_ADDR 0x07
#define LSM6DSV_FIFO_CTRL3_ADDR 0x09
#define LSM6DSV_FIFO_ODR_XL_MASK 0x0f
#define LSM6DSV_FIFO_ODR_G_MASK 0xf0

#define LSM6DSV_FIFO_CTRL4_ADDR 0x0a
#define LSM6DSV_FIFO_MODE_BYPASS_VAL 0x00
#define LSM6DSV_FIFO_MODE_CONTINUOUS_VAL 0x06

/* Interrupt FIFO flags */
#define LSM6DSV_INT_FIFO_TH 0x08
#define LSM6DSV_INT_FIFO_OVR 0x10
#define LSM6DSV_INT_FIFO_FULL 0x20

/* ODR and full-scale control registers */
#define LSM6DSV_CTRL1_ADDR 0x10
#define LSM6DSV_CTRL2_ADDR 0x11
#define LSM6DSV_ODR_REG(_sensor) (LSM6DSV_CTRL1_ADDR + (_sensor))
#define LSM6DSV_ODR_MASK 0x0f
#define LSM6DSV_ODR_480HZ_VAL LSM6DSV_ODR_TO_REG(480000)

#define LSM6DSV_CTRL6_ADDR 0x15
#define LSM6DSV_GYRO_FS_MASK 0x0f
#define LSM6DSV_GYRO_FS_MIN_VAL 125
#define LSM6DSV_GYRO_FS_MAX_REG_VAL 4

#define LSM6DSV_CTRL8_ADDR 0x17
#define LSM6DSV_ACCEL_FS_MASK 0x03
#define LSM6DSV_ACCEL_FS_MIN_VAL 2
#define LSM6DSV_ACCEL_FS_MAX_VAL 16

#define LSM6DSV_STATUS_REG 0x1e
#define LSM6DSV_STS_XLDA_UP 0x01
#define LSM6DSV_STS_GDA_UP 0x02
#define LSM6DSV_STS_XLDA_MASK 0x01
#define LSM6DSV_STS_GDA_MASK 0x02

#define LSM6DSV_GYRO_OUT_X_L_ADDR 0x22
#define LSM6DSV_ACCEL_OUT_X_L_ADDR 0x28

#define LSM6DSV_FIFO_STS1_ADDR 0x1b
#define LSM6DSV_FIFO_STS2_ADDR 0x1c
#define LSM6DSV_FIFO_DIFF_MASK 0x01ff
#define LSM6DSV_FIFO_FULL 0x2000
#define LSM6DSV_FIFO_DATA_OVR 0x4000

/* Tap and embedded functions interrupt routing registers */
#define LSM6DSV_TAP_SRC_ADDR 0x46
#define LSM6DSV_TAP_SRC_Z 0x01
#define LSM6DSV_TAP_SRC_Y 0x02
#define LSM6DSV_TAP_SRC_X 0x04
#define LSM6DSV_DOUBLE_TAP_IA 0x10
#define LSM6DSV_SINGLE_TAP_IA 0x20
#define LSM6DSV_TAP_IA 0x40

#define LSM6DSV_FUNCTIONS_ENABLE_ADDR 0x50
#define LSM6DSV_INTERRUPTS_ENABLE 0x80

#define LSM6DSV_TAP_CFG0_ADDR 0x56
#define LSM6DSV_TAP_LIR 0x01
#define LSM6DSV_TAP_XYZ_EN 0x0e

#define LSM6DSV_TAP_CFG1_ADDR 0x57
#define LSM6DSV_TAP_CFG2_ADDR 0x58
#define LSM6DSV_TAP_THS_6D_ADDR 0x59
#define LSM6DSV_TAP_THS_MASK 0x1f
/*
 * Tap threshold: 1 LSB of TAP_THS_X/Y/Z is FS_XL / 32 (in g), and 0 disables
 * tap detection on that axis.
 */
#define LSM6DSV_TAP_THS_REG(_fs)                                    \
	MIN(MAX(DIV_ROUND_NEAREST(CONFIG_GESTURE_TAP_THRES_MG * 32, \
				  (_fs) * 1000),                    \
		1),                                                 \
	    LSM6DSV_TAP_THS_MASK)

#define LSM6DSV_TAP_DUR_ADDR 0x5a
/*
 * TAP_DUR fields at ODR_XL = 480 Hz (aligned with Pixelbook settings):
 * - SHOCK[1:0] (bits 1:0): 1 LSB = 8 / ODR_XL (3 -> 50 ms)
 * - QUIET[1:0] (bits 3:2): 1 LSB = 4 / ODR_XL (3 -> 25 ms)
 * - DUR[3:0]   (bits 7:4): 1 LSB = 32 / ODR_XL (7 -> 467 ms for 500 ms window)
 */
#define LSM6DSV_TAP_SHOCK_DEFAULT 0x03
#define LSM6DSV_TAP_QUIET_DEFAULT (0x03 << 2)
#define LSM6DSV_TAP_DUR_MASK 0x0f
#define LSM6DSV_TAP_DUR_REG(_ms) \
	(MIN(((_ms) * 480) / (32 * 1000), LSM6DSV_TAP_DUR_MASK) << 4)
#define LSM6DSV_TAP_DUR_VAL                                         \
	(LSM6DSV_TAP_DUR_REG(CONFIG_GESTURE_TAP_MAX_INTERSTICE_T) | \
	 LSM6DSV_TAP_QUIET_DEFAULT | LSM6DSV_TAP_SHOCK_DEFAULT)

#define LSM6DSV_WAKE_UP_THS_ADDR 0x5b
#define LSM6DSV_SINGLE_DOUBLE_TAP 0x80

#define LSM6DSV_MD1_CFG_ADDR 0x5e
#define LSM6DSV_INT1_DOUBLE_TAP 0x08

#define LSM6DSV_FIFO_DATA_ADDR_TAG 0x78
#define LSM6DSV_TAG_SIZE 1
#define LSM6DSV_FIFO_SAMPLE_SIZE (OUT_XYZ_SIZE + LSM6DSV_TAG_SIZE)

/* Indexed by motionsensor_type, same order as the accel/gyro sensor pair */
enum lsm6dsv_dev_fifo {
	LSM6DSV_FIFO_DEV_ACCEL = MOTIONSENSE_TYPE_ACCEL,
	LSM6DSV_FIFO_DEV_GYRO = MOTIONSENSE_TYPE_GYRO,
	LSM6DSV_FIFO_DEV_NUM,
};

enum lsm6dsv_tag_fifo {
	LSM6DSV_GYRO_TAG = 0x01,
	LSM6DSV_ACC_TAG = 0x02,
};

struct lsm6dsv_fstatus {
	uint16_t len;
};

/* ODR register code (0x03 = 15 Hz .. 0x08 = 480 Hz) */
#define LSM6DSV_ODR_TO_REG(_odr) (__fls((_odr) / LSM6DSV_ODR_MIN_VAL) + 3)
#define LSM6DSV_REG_TO_ODR(_reg) (LSM6DSV_ODR_MIN_VAL << ((_reg) - 3))

#define LSM6DSV_FIFO_ODR_MASK(_s)                                          \
	((_s)->type == MOTIONSENSE_TYPE_ACCEL ? LSM6DSV_FIFO_ODR_XL_MASK : \
						LSM6DSV_FIFO_ODR_G_MASK)

#define LSM6DSV_ACCEL_NORMALIZE_FS(_fs) (1 << __fls(_fs))
#define LSM6DSV_ACCEL_FS_REG(_fs) (__fls((_fs) / 2))

#define LSM6DSV_GYRO_FS_REG(_fs) __fls(max(1, (_fs) / LSM6DSV_GYRO_FS_MIN_VAL))
#define LSM6DSV_GYRO_NORMALIZE_FS(_reg) (LSM6DSV_GYRO_FS_MIN_VAL << (_reg))

#define LSM6DSV_RESOLUTION 16
#define LSM6DSV_DISCARD_SAMPLES 3

extern const struct accelgyro_drv lsm6dsv_drv;

void lsm6dsv_interrupt(enum gpio_signal signal);

#if DT_NODE_EXISTS(DT_ALIAS(lsm6dsv_int))
#define CONFIG_ACCEL_LSM6DSV_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(SENSOR_ID(DT_ALIAS(lsm6dsv_int)))
#endif

#endif /* PLATFORM_EC_DRIVER_ACCELGYRO_LSM6DSV_H_ */
