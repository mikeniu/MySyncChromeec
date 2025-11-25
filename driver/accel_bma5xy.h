/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BMA5XY gsensor module for Chrome EC */

#ifndef __CROS_EC_ACCEL_BMA5XY_H
#define __CROS_EC_ACCEL_BMA5XY_H

#include "config.h"

#define BMA5_I2C_ADDR 0x18

/* Chip-specific registers */
#define BMA5_CHIP_ID_ADDR 0x00
#define BMA5_CHIP_ID 0xC2

#define BMA4_ERROR_ADDR 0x02
#define BMA4_FATAL_ERR_MSK 0x01
#define BMA4_CMD_ERR_POS 1
#define BMA4_CMD_ERR_MSK 0x02
#define BMA4_ERR_CODE_POS 2
#define BMA4_ERR_CODE_MSK 0x1C
#define BMA4_FIFO_ERR_POS 6
#define BMA4_FIFO_ERR_MSK 0x40
#define BMA4_AUX_ERR_POS 7
#define BMA4_AUX_ERR_MSK 0x80

#define BMA5_STATUS_ADDR 0x11
#define BMA5_STAT_DATA_RDY_ACCEL_POS 0
#define BMA5_STAT_DATA_RDY_ACCEL_MSK 0x01

#define BMA5_DATA_0_ADDR 0x18

#define BMA4_SENSORTIME_0_ADDR 0x18
#define BMA4_INT_STAT_0_ADDR 0x1C
#define BMA4_INT_STAT_1_ADDR 0x1D
#define BMA4_STEP_CNT_OUT_0_ADDR 0x1E
#define BMA4_HIGH_G_OUT_ADDR 0x1F
#define BMA4_TEMPERATURE_ADDR 0x22

#define BMA5_INT1_STATUS_0 0x12
#define BMA5_FFULL_INT BIT(2)
#define BMA5_FWM_INT BIT(1)
#define BMA5_ACC_DRDY_INT BIT(0)

#define BMA5_FIFO_LENGTH_0_ADDR 0x22
#define BMA5_FIFO_DATA_ADDR 0x24
#define BMA4_ACTIVITY_OUT_ADDR 0x27
#define BMA4_ORIENTATION_OUT_ADDR 0x28

#define BMA4_INTERNAL_STAT 0x2A
#define BMA4_ASIC_INITIALIZED 0x01

#define BMA5_ACCEL_CONF1_ADDR 0x31
#define BMA5_ACCEL_ODR_POS 0
#define BMA5_ACCEL_ODR_MSK 0x0F
#define BMA5_ACCEL_BW_POS 4
#define BMA5_ACCEL_BW_MSK 0x70
#define BMA5_ACCEL_PERFMODE_POS 7
#define BMA5_ACCEL_PERFMODE_MSK 0x80
#define BMA5_OUTPUT_DATA_RATE_1_56HZ 0x00
#define BMA5_OUTPUT_DATA_RATE_3_12HZ 0x01
#define BMA5_OUTPUT_DATA_RATE_6_25HZ 0x02
#define BMA5_OUTPUT_DATA_RATE_12_5HZ 0x03
#define BMA5_OUTPUT_DATA_RATE_25HZ 0x04
#define BMA5_OUTPUT_DATA_RATE_50HZ 0x05
#define BMA5_OUTPUT_DATA_RATE_100HZ 0x06
#define BMA5_OUTPUT_DATA_RATE_200HZ 0x07
#define BMA5_OUTPUT_DATA_RATE_400HZ 0x08
#define BMA5_OUTPUT_DATA_RATE_800HZ 0x09
#define BMA5_OUTPUT_DATA_RATE_1600HZ 0x0A
#define BMA5_OUTPUT_DATA_RATE_3200HZ 0x0B
#define BMA5_OUTPUT_DATA_RATE_6400HZ 0x0C
#define BMA4_ACCEL_OSR4_AVG1 0
#define BMA4_ACCEL_OSR2_AVG2 1
#define BMA4_ACCEL_NORMAL_AVG4 2
#define BMA4_ACCEL_CIC_AVG8 3
#define BMA4_ACCEL_RES_AVG16 4
#define BMA4_ACCEL_RES_AVG32 5
#define BMA4_ACCEL_RES_AVG64 6
#define BMA4_ACCEL_RES_AVG128 7
#define BMA4_CIC_AVG_MODE 0
#define BMA4_CONTINUOUS_MODE 1

#define BMA5_ACCEL_CONF2_ADDR 0x32
#define BMA5_ACCEL_RANGE_POS 0
#define BMA5_ACCEL_RANGE_MSK 0x03
#define BMA5_ACCEL_RANGE_2G 0
#define BMA5_ACCEL_RANGE_4G 1
#define BMA5_ACCEL_RANGE_8G 2
#define BMA5_ACCEL_RANGE_16G 3

#define BMA5_FIFO_CTRL_ADDR 0x40
#define BMA5_FIFO_RST 0x02

#define BMA5_FIFO_CONFIG_0_ADDR 0x41
#define BMA5_FIFO_ACC_EN 0x0F

#define BMA4_INT1_IO_CTRL_ADDR 0x53
#define BMA4_INT1_OUTPUT_EN BIT(3)

#define BMA5_INT1_CONF_ADDR 0x34
#define BMA5_FIFO_WM0       0x43
#define BMA5_FIFO_WM1       0x44

#define BMA5_INT_MAP_DATA_ADDR 0x36
#define BMA5_INT2_DRDY 0b00000010
#define BMA5_INT2_FWM 0b00001000
#define BMA5_INT2_FFULL 0b00100000
#define BMA5_INT1_DRDY 0b00000001
#define BMA5_INT1_FWM 0b00000100
#define BMA5_INT1_FFULL 0b00010000

#define BMA4_RESERVED_REG_5B_ADDR 0x5B
#define BMA4_RESERVED_REG_5C_ADDR 0x5C
#define BMA4_FEATURE_CONFIG_ADDR 0x5E
#define BMA4_INTERNAL_ERROR 0x5F
#define BMA4_IF_CONFIG_ADDR 0x6B
#define BMA5_FOC_ACC_CONF_VAL 0xB5

#define BMA5_OFFSET_0_ADDR 0x70
#define BMA5_OFFSET_2_ADDR 0x72
#define BMA5_OFFSET_4_ADDR 0x74

#define BMA5_ACC_CONF0_ADDR 0x30
#define BMA5_ENABLE 0x0F
#define BMA5_DISABLE 0x00

#define BMA5_ACC_CONF2_ADDR 0x32
#define BMA4_CMD_ADDR 0x7E
#define BMA4_NVM_PROG 0xA0
#define BMA4_FIFO_FLUSH 0xB0
#define BMA4_SOFT_RESET 0xB6

/* Other definitions */
#define BMA4_X_AXIS 0
#define BMA4_Y_AXIS 1
#define BMA4_Z_AXIS 2

#define BMA5_16_BIT_RESOLUTION 16

/*
 * The max positive value of accel data is 0x07FF, equal to range(g)
 * So, in order to get +1g, divide the 0x07FF by range
 */
#define BMA5_ACC_DATA_PLUS_1G(range) (0x07FF / (range))

/* For offset registers 1LSB - 3.9mg */
#define BMA5_OFFSET_ACC_MULTI_MG (3900 * 1000)
#define BMA5_OFFSET_ACC_DIV_MG 1000000

#define BMA5_FOC_SAMPLE_LIMIT 32

/* Min and Max sampling frequency in mHz */
#define BMA5_ACCEL_MIN_FREQ 12500
#define BMA5_ACCEL_MAX_FREQ 200000

#define BMA5_RANGE_TO_REG(_range)                              \
	((_range) < 8 ? BMA5_ACCEL_RANGE_2G + ((_range) / 4) : \
			BMA5_ACCEL_RANGE_8G + ((_range) / 16))

#define BMA5_REG_TO_RANGE(_reg)                          \
	((_reg) < BMA5_ACCEL_RANGE_8G ? 2 + (_reg) * 2 : \
					8 + (((_reg) - BMA5_ACCEL_RANGE_8G) >> 3))

extern const struct accelgyro_drv bma5_accel_drv;

void bma5xy_interrupt(enum gpio_signal signal);

#include <zephyr/devicetree.h>

#if DT_NODE_EXISTS(DT_ALIAS(bma5xy_int))
/*
 * Get the motion sensor ID of the BMA4xx sensor that generates the interrupt.
 * The interrupt is converted to the event and transferred to motion
 * sense task that actually handles the interrupt.
 *
 * Here, we use alias to get the motion sensor ID
 *
 * e.g) base_accel is the label of a child node in /motionsense-sensors
 * aliases {
 *     bma5xy-int = &base_accel;
 * };
 */
#define CONFIG_ACCEL_BMA5XY_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(SENSOR_ID(DT_ALIAS(bma5xy_int)))
#endif /* DT_NODE_EXISTS */
#endif /* CONFIG_ZEPHYR */

#endif /* __CROS_EC_ACCEL_BMA5XY_H */
