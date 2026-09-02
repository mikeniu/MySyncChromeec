/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_PROGRAM_RAURU_INCLUDE_RAURU_SUB_BOARD_H_
#define ZEPHYR_PROGRAM_RAURU_INCLUDE_RAURU_SUB_BOARD_H_

enum rauru_sub_board_type {
	RAURU_SB_UNKNOWN = -1, /* Uninitialised */
	RAURU_SB_NONE = 0, /* No board defined */
	RAURU_SB_REDRIVER = 1, /* USB type C Redriver */
	RAURU_SB_RETIMER = 2, /* USB type C Retimer */
};

enum rauru_sub_board_type rauru_get_sb_type(void);

#endif /* ZEPHYR_PROGRAM_RAURU_INCLUDE_RAURU_SUB_BOARD_H_ */
