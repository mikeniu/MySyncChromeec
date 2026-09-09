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
#define NODE_KB_ROTATE DT_NODELABEL(ufsc_fw_kb_layout_rotate)
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

	/* Reset the driver state */
	active_vivaldi_idx = -1;
	layout_initialized = false;

	/*
	 * set_scancode_set2() rejects columns past the active count, so make
	 * the whole matrix writable for the tests below.
	 */
	keyboard_set_cols(KEYBOARD_COLS_MAX);

	/* Initialize test scancodes */
	set_scancode_set2(2, 7, 0x0061);
	set_scancode_set2(3, 14, 0xe014);

	/* Distinct values for the chained copy test */
	set_scancode_set2(0, 10, 0x00a0);
	set_scancode_set2(0, 13, 0x00b0);
	set_scancode_set2(1, 12, 0x00c0);
	set_scancode_set2(5, 15, 0x00d0);
#ifdef CONFIG_KEYBOARD_DEBUG
	set_keycap_label(2, 7, 'a');
	set_keycap_label(3, 14, 'b');
#endif
}

ZTEST_SUITE(ufsc_keyboard_layout, NULL, NULL, ufsc_keyboard_layout_before, NULL,
	    NULL);

ZTEST(ufsc_keyboard_layout, test_layout_us2_copy)
{
	active_kb_layout_id = CBI_UFSC_VALUE_ID(NODE_KB_US2);

	hook_notify(HOOK_INIT);

	/* (3, 14) takes the scancode of (2, 7) */
	zassert_equal(get_scancode_set2(3, 14), 0x0061,
		      "scancode at (3, 14) should be 0x0061");

	/* The source is a copy, not an exchange, so it keeps its own value */
	zassert_equal(get_scancode_set2(2, 7), 0x0061,
		      "scancode at (2, 7) should be unchanged");
#ifdef CONFIG_KEYBOARD_DEBUG
	zassert_equal(get_keycap_label(3, 14), 'a',
		      "keycap label at (3, 14) should be 'a'");
#endif
}

/*
 * Entries are applied in order and later entries observe earlier writes. This
 * mirrors the JP layout on ocicat, which stages the original value of (5, 15)
 * in (0, 13) so the final entry can pick it up again.
 */
ZTEST(ufsc_keyboard_layout, test_layout_chained_copies)
{
	active_kb_layout_id = CBI_UFSC_VALUE_ID(NODE_KB_ROTATE);

	hook_notify(HOOK_INIT);

	zassert_equal(get_scancode_set2(0, 13), 0x00d0,
		      "(0, 13) should hold the original (5, 15)");
	zassert_equal(get_scancode_set2(5, 15), 0x00c0,
		      "(5, 15) should hold the original (1, 12)");
	zassert_equal(get_scancode_set2(1, 12), 0x00a0,
		      "(1, 12) should hold the original (0, 10)");
	zassert_equal(get_scancode_set2(0, 10), 0x00d0,
		      "(0, 10) should hold the original (5, 15)");
}

ZTEST(ufsc_keyboard_layout, test_layout_custom_vivaldi_config)
{
	active_kb_layout_id = CBI_UFSC_VALUE_ID(NODE_KB_CUSTOM);

	hook_notify(HOOK_INIT);

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

	/* Call board_vivaldi_keybd_idx directly without hook_notify */
	zassert_equal(board_vivaldi_keybd_idx(), 1,
		      "lazy initialization should return config 1");
	zassert_true(layout_initialized, "init should have run on demand");
}

/*
 * A board that only declares copies never sets a Vivaldi config, so
 * active_vivaldi_idx stays at -1. Initialization must still happen exactly
 * once rather than repeating on every query.
 */
ZTEST(ufsc_keyboard_layout, test_vivaldi_keybd_idx_no_config_inits_once)
{
	active_kb_layout_id = CBI_UFSC_VALUE_ID(NODE_KB_US2);

	zassert_equal(board_vivaldi_keybd_idx(), 0,
		      "fallback should return default index 0");
	zassert_true(layout_initialized, "init should have run on demand");
	zassert_equal(active_vivaldi_idx, -1, "no vivaldi-config was selected");

	/* A second query must not re-apply the patches */
	cros_cbi_ufsc_check_match_fake.call_count = 0;
	zassert_equal(board_vivaldi_keybd_idx(), 0,
		      "fallback should still return default index 0");
	zassert_equal(cros_cbi_ufsc_check_match_fake.call_count, 0,
		      "init should not run a second time");
}

ZTEST(ufsc_keyboard_layout, test_vivaldi_keybd_idx_fallback)
{
	/* No match for the vivaldi step */
	active_kb_layout_id = CBI_UFSC_VALUE_ID(NODE_KB_DEFAULT);
	active_kb_bl_id = 9999;

	zassert_equal(board_vivaldi_keybd_idx(), 0,
		      "fallback should return default index 0");
}
