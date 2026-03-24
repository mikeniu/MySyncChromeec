/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_ACCELGYRO_LSM6DSM_PUBLIC_H
#define __CROS_EC_DRIVER_ACCELGYRO_LSM6DSM_PUBLIC_H

#include "motion_sense.h"

/* Who Am I */
#define LSM6DSM_WHO_AM_I_REG 0x0f
/* LSM6DSM/LSM6DSL/LSM6DS3TR-C */
#define LSM6DSM_WHO_AM_I 0x6a
/* LSM6DS3 */
#define LSM6DS3_WHO_AM_I 0x69

/* FIFO decimator registers and bitmask */
#define LSM6DSM_FIFO_CTRL1_ADDR 0x06
#define LSM6DSM_FIFO_CTRL2_ADDR 0x07

#define LSM6DSM_FIFO_CTRL5_ADDR 0x0a
#define LSM6DSM_FIFO_CTRL5_ODR_OFF 3
#define LSM6DSM_FIFO_CTRL5_ODR_MASK (0xf << LSM6DSM_FIFO_CTRL5_ODR_OFF)
#define LSM6DSM_FIFO_CTRL5_MODE_MASK 0x07

#define LSM6DSM_ACCEL_FS_ADDR 0x10
#define LSM6DSM_ACCEL_FS_MASK 0x0c

#define LSM6DSM_ACCEL_FS_2G 0x00
#define LSM6DSM_ACCEL_FS_4G 0x02
#define LSM6DSM_ACCEL_FS_8G 0x03
#define LSM6DSM_ACCEL_FS_16G 0x01

#define LSM6DSM_OUTX_L_G 0x22
#define LSM6DSM_OUTX_L_LA 0x28

#define LSM6DSM_CTRL3_ADDR 0x12
#define LSM6DSM_SW_RESET 0x01
#define LSM6DSM_IF_INC 0x04
#define LSM6DSM_PP_OD 0x10
#define LSM6DSM_H_L_ACTIVE 0x20
#define LSM6DSM_BDU 0x40
#define LSM6DSM_BOOT 0x80

#define LSM6DSM_STATUS_REG 0x1e

/* Status register bitmask for Acc/Gyro data ready */
enum lsm6dsm_status {
	LSM6DSM_STS_DOWN = 0x00,
	LSM6DSM_STS_XLDA_UP = 0x01,
	LSM6DSM_STS_GDA_UP = 0x02
};

#define LSM6DSM_STS_XLDA_MASK 0x01
#define LSM6DSM_STS_GDA_MASK 0x02

#define LSM6DSM_FIFO_STS1_ADDR 0x3a
#define LSM6DSM_FIFO_STS2_ADDR 0x3b
#define LSM6DSM_FIFO_STS3_ADDR 0x3c
#define LSM6DSM_FIFO_STS4_ADDR 0x3d

/* Out data register */
#define LSM6DSM_FIFO_DATA_ADDR 0x3e

/*
 * Note: The specific number of samples to discard depends on the filters
 * configured for the chip, as well as the ODR being set.  For most of our
 * allowed ODRs, 5 should suffice.
 * See: ST's LSM6DSM application notes (AN4987) Tables 17 and 19 for details
 */
#define LSM6DSM_DISCARD_SAMPLES 5

#endif /* __CROS_EC_DRIVER_ACCELGYRO_LSM6DSM_PUBLIC_H */
