/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Uldren sub-board declarations */

#ifndef PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_ULDREN_SUB_BOARD_H_
#define PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_ULDREN_SUB_BOARD_H_

enum uldren_sub_board_type {
	ULDREN_SB_UNKNOWN = -1, /* Uninitialised */
	ULDREN_SB_NONE = 0, /* No board defined */
	ULDREN_SB_C = 1, /* USB type C only */
	ULDREN_SB_C_LTE = 2, /* USB type C, WWAN LTE */
};

enum uldren_sub_board_type uldren_get_sb_type(void);

#endif /* PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_ULDREN_SUB_BOARD_H_ */
