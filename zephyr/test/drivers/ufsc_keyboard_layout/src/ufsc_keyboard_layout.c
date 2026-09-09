/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "drivers/vivaldi_kbd.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_scan.h"
#include "ufsc_keyboard_layout_test.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define NODE_KB_US2 DT_NODELABEL(ufsc_fw_kb_layout_us2)
#define NODE_KB_CUSTOM DT_NODELABEL(ufsc_fw_kb_layout_custom)
#define NODE_KB_DEFAULT DT_NODELABEL(ufsc_fw_kb_layout_default)

#define NODE_BL_ABSENT DT_NODELABEL(ufsc_kb_backlight_absent)
#define NODE_BL_PRESENT DT_NODELABEL(ufsc_kb_backlight_present)

static enum cbi_ufsc_value_id active_kb_layout_id;
static enum cbi_ufsc_value_id active_kb_bl_id;

static bool mock_cbi_ufsc_check_match_layout(enum cbi_ufsc_value_id value_id)
{
	if (value_id == active_kb_layout_id || value_id == active_kb_bl_id) {
		return true;
	}
	return false;
}

static void ufsc_keyboard_layout_before(void *data)
{
	ARG_UNUSED(data);

	cros_cbi_ufsc_check_match_fake.custom_fake =
		mock_cbi_ufsc_check_match_layout;

	active_kb_layout_id = 9999;
	active_kb_bl_id = 9999;

	/* Reset columns and active vivaldi idx */
	keyboard_set_cols(KEYBOARD_COLS_MAX);
	active_vivaldi_idx = -1;

	/* Initialize test scancodes */
	set_scancode_set2(2, 7, 0x0061);
	set_scancode_set2(3, 14, 0xe014);
	set_scancode_set2(0, 1, 0x0010);
#ifdef CONFIG_KEYBOARD_DEBUG
	set_keycap_label(2, 7, 'a');
	set_keycap_label(3, 14, 'b');
#endif
}

ZTEST_SUITE(ufsc_keyboard_layout, NULL, NULL, ufsc_keyboard_layout_before, NULL,
	    NULL);

ZTEST(ufsc_keyboard_layout, test_layout_us2_swap)
{
	active_kb_layout_id = CBI_UFSC_VALUE_ID(NODE_KB_US2);

	hook_notify(HOOK_INIT);

	/* Verify scancode swap: (3, 14) gets scancode of (2, 7) */
	zassert_equal(get_scancode_set2(3, 14), 0x0061,
		      "scancode at (3, 14) should be 0x0061");
#ifdef CONFIG_KEYBOARD_DEBUG
	zassert_equal(get_keycap_label(3, 14), 'a',
		      "keycap label at (3, 14) should be 'a'");
#endif
}

ZTEST(ufsc_keyboard_layout, test_layout_custom_patch_and_cols)
{
	active_kb_layout_id = CBI_UFSC_VALUE_ID(NODE_KB_CUSTOM);

	hook_notify(HOOK_INIT);

	/* Verify fixed patch at (0, 1) */
	zassert_equal(get_scancode_set2(0, 1), 0x0042,
		      "scancode at (0, 1) should be 0x0042");

	/* Verify scan-cols */
	zassert_equal(keyboard_get_cols(), 18,
		      "keyboard_get_cols should return 18");

	/* Verify vivaldi config from custom patch */
	zassert_equal(active_vivaldi_idx, 1, "active_vivaldi_idx should be 1");
	zassert_equal(board_vivaldi_keybd_idx(), 1,
		      "board_vivaldi_keybd_idx should return 1");
}

ZTEST(ufsc_keyboard_layout, test_layout_vivaldi_bl_absent_and_present)
{
	/* Backlight absent -> default patch -> vivaldi config 0 */
	active_kb_bl_id = CBI_UFSC_VALUE_ID(NODE_BL_ABSENT);
	hook_notify(HOOK_INIT);
	zassert_equal(board_vivaldi_keybd_idx(), 0,
		      "board_vivaldi_keybd_idx should return 0");

	/* Backlight present -> custom patch -> vivaldi config 1 */
	active_vivaldi_idx = -1;
	active_kb_bl_id = CBI_UFSC_VALUE_ID(NODE_BL_PRESENT);
	hook_notify(HOOK_INIT);
	zassert_equal(board_vivaldi_keybd_idx(), 1,
		      "board_vivaldi_keybd_idx should return 1");
}

ZTEST(ufsc_keyboard_layout, test_vivaldi_keybd_idx_lazy_init)
{
	active_kb_bl_id = CBI_UFSC_VALUE_ID(NODE_BL_PRESENT);
	active_vivaldi_idx = -1;

	/* Call board_vivaldi_keybd_idx directly without hook_notify */
	zassert_equal(board_vivaldi_keybd_idx(), 1,
		      "lazy initialization should return config 1");
}

ZTEST(ufsc_keyboard_layout, test_vivaldi_keybd_idx_fallback)
{
	/* No match for vivaldi step */
	active_kb_layout_id = CBI_UFSC_VALUE_ID(NODE_KB_DEFAULT);
	active_kb_bl_id = 9999;
	active_vivaldi_idx = -1;

	zassert_equal(board_vivaldi_keybd_idx(), 0,
		      "fallback should return default index 0");
}
