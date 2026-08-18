/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Internal header with it52xx interface constants and types */

#ifndef __CROS_EC_PDC_IT52XX_H
#define __CROS_EC_PDC_IT52XX_H

#include "drivers/ucsi_v3.h"

/** length of [CCI] byte-count field */
#define IT52XX_PDC_CCI_BYTE_CNT_LEN 1

/** Total [CCI] response length: [CCI] byte count and [CCI] data. */
#define IT52XX_PDC_MAX_CCI_LENGTH \
	(IT52XX_PDC_CCI_BYTE_CNT_LEN + sizeof(union cci_event_t))

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

#endif /* __CROS_EC_PDC_IT52XX_H */
