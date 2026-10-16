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

#define LSM6DSV_CTRL8_ADDR 0x17
#define LSM6DSV_ACCEL_FS_MASK 0x03
#define LSM6DSV_ACCEL_FS_MIN_VAL 2
#define LSM6DSV_ACCEL_FS_MAX_VAL 16

#define LSM6DSV_STATUS_REG 0x1e
#define LSM6DSV_STS_XLDA_UP 0x01
#define LSM6DSV_STS_XLDA_MASK 0x01

#define LSM6DSV_ACCEL_OUT_X_L_ADDR 0x28

/* ODR register code (0x03 = 15 Hz .. 0x08 = 480 Hz) */
#define LSM6DSV_ODR_TO_REG(_odr) (__fls((_odr) / LSM6DSV_ODR_MIN_VAL) + 3)
#define LSM6DSV_REG_TO_ODR(_reg) (LSM6DSV_ODR_MIN_VAL << ((_reg) - 3))

#define LSM6DSV_ACCEL_NORMALIZE_FS(_fs) (1 << __fls(_fs))
#define LSM6DSV_ACCEL_FS_REG(_fs) (__fls((_fs) / 2))

#define LSM6DSV_RESOLUTION 16

extern const struct accelgyro_drv lsm6dsv_drv;

#endif /* PLATFORM_EC_DRIVER_ACCELGYRO_LSM6DSV_H_ */
