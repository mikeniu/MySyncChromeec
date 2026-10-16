/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LSM6DSV / LSM6DSVQ Accel driver for Chrome EC */

#ifndef PLATFORM_EC_DRIVER_ACCELGYRO_LSM6DSV_H_
#define PLATFORM_EC_DRIVER_ACCELGYRO_LSM6DSV_H_

#include "driver/accelgyro_lsm6dsv_public.h"
#include "stm_mems_common.h"

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

/* ODR and full-scale control registers */
#define LSM6DSV_CTRL1_ADDR 0x10
#define LSM6DSV_ODR_MASK 0x0f
#define LSM6DSV_ODR_480HZ_VAL LSM6DSV_ODR_TO_REG(480000)

#define LSM6DSV_CTRL8_ADDR 0x17
#define LSM6DSV_ACCEL_FS_MASK 0x03
#define LSM6DSV_ACCEL_FS_MIN_VAL 2
#define LSM6DSV_ACCEL_FS_MAX_VAL 16

#define LSM6DSV_STATUS_REG 0x1e
#define LSM6DSV_STS_XLDA_UP 0x01
#define LSM6DSV_STS_XLDA_MASK 0x01

#define LSM6DSV_ACCEL_OUT_X_L_ADDR 0x28

/* Tap and embedded functions interrupt routing registers */
#define LSM6DSV_FUNCTIONS_ENABLE_ADDR 0x50
#define LSM6DSV_INTERRUPTS_ENABLE 0x80

#define LSM6DSV_TAP_CFG0_ADDR 0x56
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

/* ODR register code (0x03 = 15 Hz .. 0x08 = 480 Hz) */
#define LSM6DSV_ODR_TO_REG(_odr) (__fls((_odr) / LSM6DSV_ODR_MIN_VAL) + 3)
#define LSM6DSV_REG_TO_ODR(_reg) (LSM6DSV_ODR_MIN_VAL << ((_reg) - 3))

#define LSM6DSV_ACCEL_NORMALIZE_FS(_fs) (1 << __fls(_fs))
#define LSM6DSV_ACCEL_FS_REG(_fs) (__fls((_fs) / 2))

#define LSM6DSV_RESOLUTION 16

extern const struct accelgyro_drv lsm6dsv_drv;

void lsm6dsv_interrupt(enum gpio_signal signal);

#if DT_NODE_EXISTS(DT_ALIAS(lsm6dsv_int))
#define CONFIG_ACCEL_LSM6DSV_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(SENSOR_ID(DT_ALIAS(lsm6dsv_int)))
#endif

#endif /* PLATFORM_EC_DRIVER_ACCELGYRO_LSM6DSV_H_ */
