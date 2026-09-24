/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Teliks sub-board declarations */

#ifndef PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_TELIKS_SUB_BOARD_H_
#define PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_TELIKS_SUB_BOARD_H_

enum teliks_sub_board_type {
	TELIKS_SB_UNKNOWN = -1, /* Uninitialised */
	TELIKS_SB_NONE = 0,
	TELIKS_SB_C = 1, /* USB type C */
};

enum teliks_sub_board_type teliks_get_sb_type(void);

#endif /* PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_TELIKS_SUB_BOARD_H_ */
