/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * For Chipset: RTK EC
 *
 * Function: RTK Flash Utility
 */

#ifndef PLATFORM_EC_ZEPHYR_CHIP_RTK_FLAME_FLASH_SPIC_H_
#define PLATFORM_EC_ZEPHYR_CHIP_RTK_FLAME_FLASH_SPIC_H_

/*********************
 *      INCLUDES
 *********************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/**********************
 *      TYPEDEFS
 **********************/
enum spic_status {
	SPIC_STATUS_ERROR = -1, /**< Generic error >*/
	SPIC_STATUS_INVALID_PARAMETER = -2, /**< The parameter is invalid >*/
	SPIC_STATUS_OKAY = 0, /**< Function executed successfully  >*/
};

enum spic_bus_width {
	SPIC_BUS_SINGLE,
	SPIC_BUS_DUAL,
	SPIC_BUS_QUAD,
};

enum spic_address_size {
	SPIC_ADDR_SIZE_8,
	SPIC_ADDR_SIZE_16,
	SPIC_ADDR_SIZE_24,
	SPIC_ADDR_SIZE_32,
};

struct spic_command {
	struct {
		enum spic_bus_width bus_width; /**< Bus width for the
					       instruction
					       >*/
		uint8_t value; /**< Instruction value >*/
		uint8_t disabled; /**< Instruction phase skipped if disabled is
				     set to true >*/
	} instruction;
	struct {
		enum spic_bus_width bus_width; /**< Bus width for the address
						  >*/
		enum spic_address_size size; /**< Address size >*/
		uint32_t value; /**< Address value >*/
		uint8_t disabled; /**< Address phase skipped if disabled is set
				     to true >*/
	} address;
	struct {
		enum spic_bus_width bus_width; /**< Bus width for alternative
						  >*/
		uint8_t size; /**< Alternative size >*/
		uint32_t value; /**< Alternative value >*/
		uint8_t disabled; /**< Alternative phase skipped if disabled is
				     set to true >*/
	} alt;
	uint8_t dumm_count; /**< Dumm cycles count >*/
	struct {
		enum spic_bus_width bus_width; /**< Bus width for data >*/
	} data;
};

/**********************
 *  GLOBAL PROTOTYPES
 **********************/
enum spic_status spic_init(uint8_t hz_select, uint8_t mode);
enum spic_status spic_init_direct(uint8_t hz_select, uint8_t mode);
enum spic_status spic_frequency(uint8_t hz_select);
enum spic_status spic_write(const struct spic_command *command,
			    const void *data, uint32_t *length);
enum spic_status spic_read(const struct spic_command *command, void *data,
			   uint32_t *length);

#endif /* PLATFORM_EC_ZEPHYR_CHIP_RTK_FLAME_FLASH_SPIC_H_ */
