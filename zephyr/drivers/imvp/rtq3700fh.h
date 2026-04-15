/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_IMVP_RTQ3700FH_H_
#define ZEPHYR_DRIVERS_IMVP_RTQ3700FH_H_

#include <zephyr/device.h>
#include <zephyr/types.h>

/* RTQ3700FH Pages (Quadruple-Channel) */
#define RTQ3700FH_PAGE_VCCCORE 0x0
#define RTQ3700FH_PAGE_VCCGT 0x1
#define RTQ3700FH_PAGE_VCCSA 0x2
#define RTQ3700FH_PAGE_VCC_LP_ECORE 0x3

/* Standard PMBus Page Register */
#define PAGE_SET_REG 0x00

/* ==========================================================================
 * Device & Manufacturer ID (Page 0)
 * ========================================================================== */
#define MFR_ID_REG 0x99
#define MFR_ID 0x5452

#define MFR_MODEL_REG 0x9A
#define MFR_MODEL 0x4846

#define MFR_REVISION_REG 0x9B
#define MFR_REVISION 0x3230

#define MFR_CONFIG_ID_REG 0x9E
#define MFR_CONFIG_ID 0x00

#define MFR_PROJECT_CODE_REG 0x9F
#define MFR_PROJECT_CODE 0x0000

/* ==========================================================================
 * NVM Programming & Status
 * ========================================================================== */
/* NVM Action Commands (Page 50) */
#define STORE_USER_ALL_REG 0x15
#define RESTORE_USER_ALL_REG 0x16

/* NVM Status (Page 50, Register 0xED) */
#define NVM_STAT_REG 0xED
#define NVM_RELOAD_STAT_BIT 0x7
#define NVM_PRGRM_FINISH_STAT_BIT 0x6
#define NVM_READY_STAT_VAL 0xE0

/* Firmware Version & Checksum (Page 0) */
#define FW_VERSION_REG 0xEF
#define FW_VERSION 0x0000
#define NVM_CHECKSUM_REG 0xF0 /* Protocol: Read32 (4 bytes) */

/* NVM Password Control (Page 0) */
#define LOCK_UNLOCK_REG 0xFC
#define UNLOCK_PASSWORD 0x2454

/* ==========================================================================
 * Data Structures & Function Prototypes
 * ========================================================================== */

/* PMBus uses mostly 16-bit words for values */
struct rtq3700fh_info {
	uint8_t page;
	uint8_t reg;
	uint16_t val;
};

#endif /* ZEPHYR_DRIVERS_IMVP_RTQ3700FH_H_ */