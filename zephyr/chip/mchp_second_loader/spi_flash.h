/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_CHIP_MCHP_SECOND_LOADER_SPI_FLASH_H_
#define PLATFORM_EC_ZEPHYR_CHIP_MCHP_SECOND_LOADER_SPI_FLASH_H_

#define FLASH_DATA_COMPARE_ERROR (1 << 0)

void spi_flash_init(uint32_t spi_util_cmd);
enum failure_resp_type spi_flash_program_sector(uint32_t sector_address,
						uint8_t *input_data_ptr);
enum failure_resp_type spi_flash_sector_erase(uint32_t addr);
enum failure_resp_type
spi_splash_check_sector_content_same(uint32_t sector_address, uint8_t *status,
				     uint8_t *input_data_ptr);
#endif /* PLATFORM_EC_ZEPHYR_CHIP_MCHP_SECOND_LOADER_SPI_FLASH_H_ */
