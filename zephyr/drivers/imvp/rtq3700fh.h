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

/* NVM Programming & Status (Based on RTQ3700FH guide: 0xED) */
#define NVM_STAT_REG 0xED
#define NVM_RELOAD_STAT_BIT 0x7
#define NVM_PRGRM_FINISH_STAT_BIT 0x6
#define NVM_READY_STAT_VAL 0xE0

/* Device & Manufacturer ID */
#define PRODUCT_ID_REG 0xAD
#define PRODUCT_ID 0x4448

#define MFR_ID_REG 0x99
#define MFR_ID 0x5452

/* Standard PMBus Page Register */
#define PAGE_SET_REG 0x00

/* PMBus uses mostly 16-bit words for values */
struct rtq3700fh_info {
	uint8_t page;
	uint8_t reg;
	uint16_t val;
};

int rtq3700fh_read_reg(const struct device *dev, uint8_t reg, uint16_t *val);

int rtq3700fh_write_reg(const struct device *dev, uint8_t reg, uint16_t val);

int rtq3700fh_set_cfg_mode(const struct device *dev, const uint8_t *seq,
			   uint8_t seq_len);

int rtq3700fh_set_page(const struct device *dev, uint8_t page);

int rtq3700fh_load_config(const struct device *dev);

int rtq3700fh_store_config(const struct device *dev);

#endif /* ZEPHYR_DRIVERS_IMVP_RTQ3700FH_H_ */
