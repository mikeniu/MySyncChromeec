/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Joxer sub-board declarations */

#ifndef ZEPHYR_PROGRAM_NISSA_INCLUDE_JOXER_SUB_BOARD_H_
#define ZEPHYR_PROGRAM_NISSA_INCLUDE_JOXER_SUB_BOARD_H_

enum joxer_sub_board_type {
	JOXER_SB_UNKNOWN = -1, /* Uninitialised */
	JOXER_SB = 0,
	JOXER_SB_C = 1, /* USB type C */
};

enum joxer_sub_board_type joxer_get_sb_type(void);

#endif /* ZEPHYR_PROGRAM_NISSA_INCLUDE_JOXER_SUB_BOARD_H_ */
