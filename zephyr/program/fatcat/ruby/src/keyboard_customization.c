/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_cbi.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/logging/log.h>

#include <drivers/boot_keys.h>
#include <drivers/fn_keys.h>
#include <drivers/runtime_keys.h>
#include <drivers/scancode_set2.h>
#include <drivers/vivaldi_kbd.h>

LOG_MODULE_REGISTER(ruby_kb, LOG_LEVEL_INF);

#define CROS_EC_KEYBOARD_NODE DT_CHOSEN(cros_ec_keyboard)

/* Actual key mask of the fix keyboard layout, the default one is specified in
 * the keyboard node.
 */
static const uint8_t kb_fix_actual_key_mask[] = {
	0x53, /* C0 */
	0x6f, /* C1 */
	0x7d, /* C2 */
	0x2d, /* C3 */
	0xff, /* C4 */
	0xef, /* C5 */
	0xaf, /* C6 */
	0xfd, /* C7 */
	0xef, /* C8 */
	0xdd, /* C9 */
	0xe5, /* C10 */
	0xfd, /* C11 */
	0xef, /* C12 */
	0xef, /* C13 */
	0xcf, /* C14 */
	0xb9, /* C15 */
	0xfd, /* C16 */
	0xc2, /* C17 */
};
BUILD_ASSERT(ARRAY_SIZE(kb_fix_actual_key_mask) ==
	     DT_PROP(CROS_EC_KEYBOARD_NODE, col_size));

static bool kb_layout_is_fix(void)
{
	return cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(ufsc_kb_layout_fix)));
}

int8_t board_vivaldi_keybd_idx(void)
{
	return kb_layout_is_fix() ? VIVALDI_CFG_IDX(kbd_config_fix) :
				    VIVALDI_CFG_IDX(kbd_config_0);
}

int8_t board_boot_keys_idx(void)
{
	return kb_layout_is_fix() ? BOOT_KEYS_CFG_IDX(boot_keys_fix) :
				    BOOT_KEYS_CFG_IDX(boot_keys_default);
}

int8_t board_runtime_keys_idx(void)
{
	return kb_layout_is_fix() ? RUNTIME_KEYS_CFG_IDX(runtime_keys_fix) :
				    RUNTIME_KEYS_CFG_IDX(runtime_keys_default);
}

int8_t board_fn_keys_idx(void)
{
	return kb_layout_is_fix() ? FN_KEYS_CFG_IDX(fn_keys_fix) :
				    FN_KEYS_CFG_IDX(fn_keys_default);
}

/* Update the actual key mask before the boot keys are checked, so that the
 * keys of the fix layout are scanned.
 */
static int keyboard_layout_init(void)
{
	const struct device *dev = DEVICE_DT_GET(CROS_EC_KEYBOARD_NODE);

	if (!kb_layout_is_fix()) {
		return 0;
	}

	for (uint8_t col = 0; col < ARRAY_SIZE(kb_fix_actual_key_mask); col++) {
		for (uint8_t row = 0; row < KEYBOARD_ROWS; row++) {
			input_kbd_matrix_actual_key_mask_set(
				dev, row, col,
				kb_fix_actual_key_mask[col] & BIT(row));
		}
	}

	return 0;
}
SYS_INIT(keyboard_layout_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* Runs before vivaldi_kbd_init, which then sets the top row scancodes. */
test_export_static void keyboard_matrix_init(void)
{
	if (kb_layout_is_fix()) {
		set_scancode_set2_table(SCANCODE_SET2_IDX(scancodes_fix));
		LOG_INF("UFSC KB_LAYOUT=FIX");
	} else {
		set_scancode_set2_table(SCANCODE_SET2_IDX(scancodes_default));
		LOG_INF("UFSC KB_LAYOUT=DEFAULT");
	}
}
DECLARE_HOOK(HOOK_INIT, keyboard_matrix_init, HOOK_PRIO_POST_FIRST);

#ifdef CONFIG_KEYBOARD_DEBUG
static uint8_t keycap_label[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_F11, KLLI_ESC, KLLI_TAB, '~', 'a', '1', 'z', 'u' },
	{ KLLI_F1, KLLI_F4, KLLI_F3, KLLI_F2, 'd', ',', '3', 'i' },
	{ 'b', 'g', 't', '5', 'f', '.', '4', 'o' },
	{ KLLI_F10, KLLI_F7, KLLI_F6, 's', KLLI_F5, '/', '2', 'p' },
	{ 'n', KLLI_F12, ']', KLLI_F13, 'k', 'c', '8', 'q' },
	{ KLLI_UNKNO, 'h', 'y', '-', 'j', KLLI_SPACE, '0', 'w' },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_L_SHT, KLLI_UNKNO, KLLI_R_SHT },
	{ '=', '\'', '[', '6', ';', 'x', '7', 'e' },
	{ KLLI_UNKNO, KLLI_F9, KLLI_UNKNO, KLLI_UNKNO, 'l', 'v', '9',
	  'r' }, /*delete at 2*/
	{ KLLI_R_ALT, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_F14, KLLI_B_SPC, KLLI_F8, KLLI_UNKNO, KLLI_ENTER, 'm', KLLI_DOWN,
	  KLLI_UP },
	{ KLLI_F15, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_RIGHT, KLLI_LEFT },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_L_ALT, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_SEARC, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
};

uint8_t get_keycap_label(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		return keycap_label[col][row];
	return KLLI_UNKNO;
}

void set_keycap_label(uint8_t row, uint8_t col, uint8_t val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		keycap_label[col][row] = val;
}
#endif
