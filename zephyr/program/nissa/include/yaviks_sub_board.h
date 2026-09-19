/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Yaviks sub-board declarations */

#ifndef PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_YAVIKS_SUB_BOARD_H_
#define PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_YAVIKS_SUB_BOARD_H_

enum yaviks_sub_board_type {
	YAVIKS_SB_UNKNOWN = -1, /* Uninitialised */
	YAVIKS_SB_A = 0, /* Only USB type A */
	YAVIKS_SB_C_A = 1, /* USB type C, USB type A */
};

enum yaviks_sub_board_type yaviks_get_sb_type(void);

#endif /* PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_YAVIKS_SUB_BOARD_H_ */
