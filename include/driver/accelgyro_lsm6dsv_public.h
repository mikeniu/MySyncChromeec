/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LSM6DSV / LSM6DSVQ Accel and Gyro driver for Chrome EC */

#ifndef PLATFORM_EC_INCLUDE_DRIVER_ACCELGYRO_LSM6DSV_PUBLIC_H_
#define PLATFORM_EC_INCLUDE_DRIVER_ACCELGYRO_LSM6DSV_PUBLIC_H_

/*
 * 7-bit address is 110101xb. Where 'x' is determined
 * by the voltage on the ADDR pin
 */
#define LSM6DSV_ADDR0_FLAGS 0x6a
#define LSM6DSV_ADDR1_FLAGS 0x6b

/* Absolute maximum rate for Acc and Gyro sensors */
#define LSM6DSV_ODR_MIN_VAL 15000
#define LSM6DSV_ODR_MAX_VAL MOTION_MAX_SENSOR_FREQUENCY(480000, 15000)

/* Who Am I */
#define LSM6DSV_WHO_AM_I_REG 0x0f
#define LSM6DSV_WHO_AM_I 0x70

/* Interface configuration register */
#define LSM6DSV_IF_CFG_ADDR 0x03
#define LSM6DSV_PP_OD 0x08
#define LSM6DSV_H_LACTIVE 0x10

/* INT1 / INT2 control registers */
#define LSM6DSV_INT1_CTRL 0x0d
#define LSM6DSV_INT2_CTRL 0x0e

/* CTRL3 register */
#define LSM6DSV_CTRL3_ADDR 0x12
#define LSM6DSV_SW_RESET 0x01
#define LSM6DSV_IF_INC 0x04
#define LSM6DSV_BDU 0x40

#endif /* PLATFORM_EC_INCLUDE_DRIVER_ACCELGYRO_LSM6DSV_PUBLIC_H_ */
