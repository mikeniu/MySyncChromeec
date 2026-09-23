/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_MELIKS_INCLUDE_BOARD_H_
#define PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_MELIKS_INCLUDE_BOARD_H_

#define I2C_ADDR_ISL98607_FLAGS 0x29

/* ISL98607 registers and value */
/* Enable VP / VN / VBST */
#define ISL98607_REG_ENABLE 0x05
#define ISL98607_VP_VN_VBST_EN 0x07
#define ISL97607_VP_VN_VBST_DIS 0x00

/* VBST Voltage Adjustment */
#define ISL98607_REG_VBST_OUT 0x06
#define ISL98607_VBST_OUT_5P65 0x0a

/* VN Voltage Adjustment */
#define ISL98607_REG_VN_OUT 0x08
#define ISL98607_VN_OUT_5P5 0x0a

/* VP Voltage Adjustment */
#define ISL98607_REG_VP_OUT 0x09
#define ISL98607_VP_OUT_5P5 0x0a

#define ISL98607_REG_VBST_CNTRL 0x0D
#define ISL98607_VBST_PFM_MODE_DISABLE (1 << 3)

#endif /* PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_MELIKS_INCLUDE_BOARD_H_ */
