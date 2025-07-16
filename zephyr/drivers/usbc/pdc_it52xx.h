/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Internal header with it52xx interface constants and types */

#ifndef __CROS_EC_PDC_IT52XX_H
#define __CROS_EC_PDC_IT52XX_H

#include "drivers/pdc.h"

#include <string.h>

/**
 * @brief RTS54XX I2C block read command
 */
//define RTS54XX_BLOCK_READ_CMD 0x80
/* CCI command length: 1byte byte count and 4bytes data */
#define PDC_MAX_CCI_LENGTH 0x05

/**
 * @brief Offsets of data fields in the GET_IC_STATUS response
 *
 * These are based on the ITE FW guide version 0.7.
 *
 * "Data Byte 0" is the first byte after "Byte Count" and is available
 * at .rd_buf[1].
 */
#define IT52XX_GET_IC_STATUS_VID_L 1
#define IT52XX_GET_IC_STATUS_VID_H 2
#define IT52XX_GET_IC_STATUS_PID_L 3
#define IT52XX_GET_IC_STATUS_PID_H 4
#define IT52XX_GET_IC_STATUS_FWVER_MINOR_INDEX 5
#define IT52XX_GET_IC_STATUS_FWVER_MAJOR_INDEX 6
#define IT52XX_GET_IC_STATUS_FWVER_UNUSED0_INDEX 7
#define IT52XX_GET_IC_STATUS_FWVER_UNUSED1_INDEX 8
#define IT52XX_GET_IC_STATUS_PD_REV_MINOR_INDEX 9
#define IT52XX_GET_IC_STATUS_PD_REV_MAJOR_INDEX 10
#define IT52XX_GET_IC_STATUS_PD_VER_MINOR_INDEX 11
#define IT52XX_GET_IC_STATUS_PD_VER_MAJOR_INDEX 12
/* UCSI only supports on it5271 flash code, so this value is always 1. */
#define IT52XX_GET_IC_STATUS_RUNNING_FLASH_CODE_INDEX 13
/* This value only be flash bank1 or bank2 */
#define IT52XX_GET_IC_STATUS_RUNNING_FLASH_BANK_INDEX 14
#define IT52XX_GET_IC_STATUS_PROG_NAME_STR_INDEX 15
//define IT52XX_GET_IC_STATUS_PROG_NAME_STR_LEN 12 //use USB_PD_CHIP_INFO_PROJECT_NAME_LEN
/* driver_name and no_fw_update are handled by EC, so skip them. */
#define IT52XX_GET_IC_STATUS_EXTRA_INDEX 28



#define RTS54XX_GET_IC_STATUS_SBU_MUX_MODE_OFFSET 39

static inline void
it52xx_unpack_get_ic_status_response(uint8_t *rx_buf, struct pdc_info_t *info)
{
	/* ITE VID: Data Byte0..1 (little-endian) */
	info->vid = rx_buf[IT52XX_GET_IC_STATUS_VID_H] << 8 |
		    rx_buf[IT52XX_GET_IC_STATUS_VID_L];

	/* ITE PID: Data Byte2..3 (little-endian) */
	info->pid = rx_buf[IT52XX_GET_IC_STATUS_PID_H] << 8 |
		    rx_buf[IT52XX_GET_IC_STATUS_PID_L];

	/* ITE FW version: Data Byte4..5 (little-endian) */
	info->fw_version =
		rx_buf[IT52XX_GET_IC_STATUS_FWVER_MAJOR_INDEX] << 16 |
		rx_buf[IT52XX_GET_IC_STATUS_FWVER_MINOR_INDEX] << 8;

	/* ITE PD Revision: Data Byte8..9 (little-endian) */
	info->pd_revision = rx_buf[IT52XX_GET_IC_STATUS_PD_REV_MAJOR_INDEX] << 8 |
			    rx_buf[IT52XX_GET_IC_STATUS_PD_REV_MINOR_INDEX];

	/* ITE PD Version: Data Byte10..11 (little-endian) */
	info->pd_version = rx_buf[IT52XX_GET_IC_STATUS_PD_VER_MAJOR_INDEX] << 8 |
			   rx_buf[IT52XX_GET_IC_STATUS_PD_VER_MINOR_INDEX];

	/* ITE is running flash code: Data Byte12 */
	info->is_running_flash_code =
		rx_buf[IT52XX_GET_IC_STATUS_RUNNING_FLASH_CODE_INDEX];

	/* ITE running flash bank offset: Data Byte13 */
	info->running_in_flash_bank =
		rx_buf[IT52XX_GET_IC_STATUS_RUNNING_FLASH_BANK_INDEX];
}

/* FW project name length should not exceed the max length supported in struct
 * pdc_info_t
 */
BUILD_ASSERT(USB_PD_CHIP_INFO_PROJECT_NAME_LEN <=
	     (sizeof(((struct pdc_info_t *)0)->project_name) - 1));

#define RTS54XX_GET_IC_STATUS_SBU_MUX_MODE_NORMAL 0
#define RTS54XX_GET_IC_STATUS_SBU_MUX_MODE_FORCE_DBG 1

/**
 * @brief PDC Command states
 */
enum cmd_sts_t {
	/** Command has not been started */
	CMD_RESET_COMPLETE = 0x08,
	CMD_ACK_COMPLETE = 0x20,
	CMD_BUSY = 0x10,
	/** Command has completed */
	CMD_DONE = 0x80,
	/** Command has been started but has not completed */
	//CMD_DEFERRED = 0x10,
	/** Command completed with error. Send GET_ERROR_STATUS for details */
	CMD_ERROR = 0xC0
};

/**
 * @brief [CCI] message of the PDC
 */
union cci_message_t {
	/* rtk cci only reply one byte */
	//struct {
	//	/** Command status */
	//	uint8_t cmd_sts : 2;
	//	/** Length of data read to read */
	//	uint8_t data_len : 6;
	//};
	//uint8_t raw_value;

	/* ite(spec) cci reply five bytes ByteCount - xx xx(MSGIN len) xx xx(status: ... /busy/ACK/error/CC) */
	struct {
		/** Byte count of CCI Command */
		uint8_t cci_byte_cnt;
		/** End of message indicator */
		uint8_t end_of_msgi : 1;
		/** Connector change indicator */
		uint8_t cci : 7;
		/** Data length */
		uint8_t data_len;
		/** Vendor defined message indicator */
		uint8_t vdmi : 1;
		/** Reserved */
		uint8_t Reserved : 6;
		/** Security request indicator */
		uint8_t sri : 1;
		/** Command status */
		uint8_t cmd_sts;
	};
	uint8_t raw_value[PDC_MAX_CCI_LENGTH];
};

#endif /* __CROS_EC_PDC_IT52XX_H */
