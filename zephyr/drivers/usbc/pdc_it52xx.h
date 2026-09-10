/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Internal header with it52xx interface constants and types */

#ifndef __CROS_EC_PDC_IT52XX_H
#define __CROS_EC_PDC_IT52XX_H

#include <stdint.h>

/**
 * @brief PDC [CCI] command length
 *
 * Includes a one-byte count and four bytes of data.
 */
#define IT52XX_PDC_MAX_CCI_LENGTH 5

/**
 * @brief PDC [CCI] Command states
 */
enum it52xx_cmd_status {
	/** PDC reset completed */
	IT52XX_CMD_RESET_COMPLETE = 0x08,
	/** PDC is busy */
	IT52XX_CMD_BUSY = 0x10,
	/** ACK command completed */
	IT52XX_CMD_ACK_COMPLETE = 0x20,
	/** Command has completed */
	IT52XX_CMD_DONE = 0x80,
	/** Recognized command is unsupported and completed without an error */
	IT52XX_CMD_NOT_SUPPORTED = 0x82,
	/**
	 * Recognized command is supported and completed with an error.
	 * Send GET_ERROR_STATUS for details.
	 */
	IT52XX_CMD_ERROR = 0xC0,
	/** Unrecognized command is unsupported and completed with an error */
	IT52XX_CMD_NOT_SUPPORTED_ERROR = 0xC2,
};

/**
 * @brief PDC [CCI] message
 */
union it52xx_cci_message {
	struct {
		/** Byte count of CCI Command */
		uint8_t cci_byte_cnt;
		/** End of message indicator */
		uint8_t end_of_message : 1;
		/** Connector change indicator */
		uint8_t cci : 7;
		/** Data length */
		uint8_t data_len;
		/** Vendor defined message indicator */
		uint8_t vdmi : 1;
		/** Reserved */
		uint8_t reserved : 6;
		/** Security request indicator */
		uint8_t sri : 1;
		/** Command status */
		uint8_t cmd_sts;
	};
	uint8_t raw_value[IT52XX_PDC_MAX_CCI_LENGTH];
};

#endif /* __CROS_EC_PDC_IT52XX_H */
