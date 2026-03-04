/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_cbi.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_customization.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"

struct keyboard_type key_typ = {
	.col_esc = KEYBOARD_COL_ESC,
	.row_esc = KEYBOARD_ROW_ESC,
	.col_down = KEYBOARD_COL_DOWN,
	.row_down = KEYBOARD_ROW_DOWN,
	.col_left_shift = KEYBOARD_COL_LEFT_SHIFT,
	.row_left_shift = KEYBOARD_ROW_LEFT_SHIFT,
	.col_refresh = KEYBOARD_COL_REFRESH,
	.row_refresh = KEYBOARD_ROW_REFRESH,
	.col_right_alt = KEYBOARD_COL_RIGHT_ALT,
	.row_right_alt = KEYBOARD_ROW_RIGHT_ALT,
	.col_left_alt = KEYBOARD_COL_LEFT_ALT,
	.row_left_alt = KEYBOARD_ROW_LEFT_ALT,
	.col_key_r = KEYBOARD_COL_KEY_R,
	.row_key_r = KEYBOARD_ROW_KEY_R,
	.col_key_h = KEYBOARD_COL_KEY_H,
	.row_key_h = KEYBOARD_ROW_KEY_H,
};

int keyboard_choose(void)
{
	uint32_t val;

	cros_cbi_get_fw_config(KB, &val);

	return val;
}

struct boot_key_entry boot_key_list[] = {
	[BOOT_KEY_ESC] = { KEYBOARD_COL_ESC, KEYBOARD_ROW_ESC },
	[BOOT_KEY_DOWN_ARROW] = { KEYBOARD_COL_DOWN, KEYBOARD_ROW_DOWN },
	[BOOT_KEY_LEFT_SHIFT] = { KEYBOARD_COL_LEFT_SHIFT,
				  KEYBOARD_ROW_LEFT_SHIFT },
	[BOOT_KEY_REFRESH] = { KEYBOARD_COL_REFRESH, KEYBOARD_ROW_REFRESH },
};
BUILD_ASSERT(ARRAY_SIZE(boot_key_list) == BOOT_KEY_COUNT);

void key_choose(void)
{
	if (keyboard_choose() == 0) {
		key_typ.col_left_alt = KEYBOARD2_COL_LEFT_ALT;
		key_typ.row_left_alt = KEYBOARD2_ROW_LEFT_ALT;
		ccprintf("keyboard_choose 0");
	} else {
		ccprintf("keyboard_choose 1");
	}
}

static void board_init(void)
{
	key_choose();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_PRE_DEFAULT);
