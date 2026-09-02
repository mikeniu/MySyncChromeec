/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_FINGERPRINT_DRIVERS_EGIS660_INCLUDE_CONFIG_CHIP_H_
#define ZEPHYR_TEST_FINGERPRINT_DRIVERS_EGIS660_INCLUDE_CONFIG_CHIP_H_

#define CONFIG_FLASH_WRITE_IDEAL_SIZE \
	DT_PROP(DT_INST(0, soc_nv_flash), write_block_size)
#define CONFIG_FLASH_ERASE_SIZE \
	DT_PROP(DT_INST(0, soc_nv_flash), erase_block_size)

#endif /* ZEPHYR_TEST_FINGERPRINT_DRIVERS_EGIS660_INCLUDE_CONFIG_CHIP_H_ */
