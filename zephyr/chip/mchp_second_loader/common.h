/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_CHIP_MCHP_SECOND_LOADER_COMMON_H_
#define ZEPHYR_CHIP_MCHP_SECOND_LOADER_COMMON_H_

#include <zephyr/devicetree.h>

#define SECTOR_SIZE 4096

#if DT_NODE_EXISTS(DT_NODELABEL(cbi_flash))
#define CBI_FLASH_NODE DT_NODELABEL(cbi_flash)
#define CBI_FLASH_OFFSET DT_PROP(CBI_FLASH_NODE, offset)
#endif

enum failure_resp_type {
	NO_FAILURE = 0,
	PACKET_PAYLOAD_ILLEGAL_LEN,
	PACKET_CRC_FAILURE,
	HEADER_PACKET_ILLEGAL_OFFSET,
	HEADER_PACKET_INVALID,
	PGM_PACKET_ILLEGAL_OFFSET,
	PGM_FLASH_DATA_LEN_INCORRECT,
	SPI_OPERATION_FAILURE,
	/* Internal errors without a corresponding host response type */
	INTERNAL_ERROR_START, /* Placeholder */
	BOARD_INIT_ERR,
	SERIAL_RECV_TIMEOUT
};

#endif /* ZEPHYR_CHIP_MCHP_SECOND_LOADER_COMMON_H_ */
