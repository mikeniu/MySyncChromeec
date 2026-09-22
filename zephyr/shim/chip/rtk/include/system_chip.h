/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_CHIP_RTK_INCLUDE_SYSTEM_CHIP_H_
#define PLATFORM_EC_ZEPHYR_SHIM_CHIP_RTK_INCLUDE_SYSTEM_CHIP_H_

void system_download_from_flash(uint32_t srcAddr, uint32_t dstAddr,
				uint32_t size, uint32_t exeAddr);

/* Begin and end address for little FW; defined in linker script */
extern unsigned int __flash_lplfw_start;
extern unsigned int __flash_lplfw_end;

#endif /* PLATFORM_EC_ZEPHYR_SHIM_CHIP_RTK_INCLUDE_SYSTEM_CHIP_H_ */
