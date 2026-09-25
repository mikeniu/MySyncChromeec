/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Anraggar sub-board declarations */

#ifndef PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_ANRAGGAR_SUB_BOARD_H_
#define PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_ANRAGGAR_SUB_BOARD_H_

enum anraggar_sub_board_type {
	ANRAGGAR_SB_UNKNOWN = -1, /* Uninitialised */
	ANRAGGAR_SB_NONE = 0,
	ANRAGGAR_SB_C = 1, /* USB type C */
};

enum anraggar_sub_board_type anraggar_get_sb_type(void);

#endif /* PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_ANRAGGAR_SUB_BOARD_H_ */
