/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_CHIP_MCHP_SECOND_LOADER_CRC32_H_
#define PLATFORM_EC_ZEPHYR_CHIP_MCHP_SECOND_LOADER_CRC32_H_

#include <stdint.h>
#include <stdlib.h>

uint32_t crc32_update(uint32_t crc, const unsigned char *data, size_t data_len);
uint32_t crc32_init(void);
uint32_t crc32_finalize(uint32_t crc);

#endif /* PLATFORM_EC_ZEPHYR_CHIP_MCHP_SECOND_LOADER_CRC32_H_ */
