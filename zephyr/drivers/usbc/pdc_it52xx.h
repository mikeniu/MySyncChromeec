/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Internal header with it52xx interface constants and types */

#ifndef __CROS_EC_PDC_IT52XX_H
#define __CROS_EC_PDC_IT52XX_H

#include "drivers/ucsi_v3.h"

#include <zephyr/sys/util.h>

struct it52xx_cci_response {
	/* [CCI] response byte count. */
	uint8_t byte_count;
	/* [CCI] data structure. */
	union cci_event_t cci;
} __packed;

BUILD_ASSERT(sizeof(struct it52xx_cci_response) == 5);

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
	/**
	 * Recognized command is not supported by the PDC firmware
	 * (e.g. SET_RETIMER_MODE).
	 * Not Supported indicator = 1b, Command Completed
	 * indicator = 1b; Error indicator = 0b.
	 */
	IT52XX_CMD_NOT_SUPPORTED = 0x82,
	/**
	 * Supported command completed with an error
	 * (e.g. an invalid command parameter).
	 * Not Supported indicator = 0b, Command Completed
	 * indicator = 1b; Error indicator = 1b.
	 * Send GET_ERROR_STATUS for details.
	 */
	IT52XX_CMD_ERROR = 0xC0,
	/**
	 * Unrecognized command opcode (e.g. 0xff).
	 * Not Supported indicator = 1b, Command Completed
	 * indicator = 1b, Error indicator = 1b.
	 * GET_ERROR_STATUS reports Unrecognized Command.
	 */
	IT52XX_CMD_NOT_SUPPORTED_ERROR = 0xC2,
};

#endif /* __CROS_EC_PDC_IT52XX_H */
