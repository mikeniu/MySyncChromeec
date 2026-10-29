/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Gothrax sub-board declarations */

#ifndef PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_GOTHRAX_SUB_BOARD_H_
#define PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_GOTHRAX_SUB_BOARD_H_

#include <zephyr/devicetree.h>

enum gothrax_sub_board_type {
	GOTHRAX_SB_UNKNOWN = -1, /* Uninitialised */
	GOTHRAX_SB_NONE = 0, /* No board defined */
	GOTHRAX_SB_C_A = 1, /* USB type C, USB type A */
	GOTHRAX_SB_C_A_LTE = 2, /* USB type C, USB type A, WWAN LTE */
#if DT_NODE_EXISTS(DT_PATH(nissa_fw_config, sub_board, sub_board_6))
	GOTHRAX_SB_A = 3, /* USB type A */
	GOTHRAX_SB_HDMI_A_LTE = 6, /* HDMI, USB type A, WWAN LTE */
#else
	GOTHRAX_SB_HDMI_A_LTE = 3, /* HDMI, USB type A, WWAN LTE */
#endif
};

enum gothrax_sub_board_type gothrax_get_sb_type(void);

#endif /* PLATFORM_EC_ZEPHYR_PROGRAM_NISSA_INCLUDE_GOTHRAX_SUB_BOARD_H_ */
