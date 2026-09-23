/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "stubs.h"

#include <zephyr/ztest.h>

int kb_init(void);

static uint32_t fake_board_id;

static int fake_cbi_get_board_version(uint32_t *version)
{
	*version = fake_board_id;
	return EC_SUCCESS;
}

static void test_keyboard_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(cbi_get_board_version);
	RESET_FAKE(input_kbd_matrix_actual_key_mask_set);
	fake_board_id = 0;
	cbi_get_board_version_fake.custom_fake = fake_cbi_get_board_version;
}

ZTEST_SUITE(mica_keyboard, NULL, NULL, test_keyboard_before, NULL, NULL);

/* Test keyboard init failure on CBI read error */
ZTEST(mica_keyboard, test_kb_init_cbi_fail)
{
	cbi_get_board_version_fake.custom_fake = NULL;
	cbi_get_board_version_fake.return_val = EC_ERROR_UNKNOWN;

	zassert_equal(kb_init(), 0);
	zassert_equal(input_kbd_matrix_actual_key_mask_set_fake.call_count,
		      18 * 8);
}

/* Test keyboard initialization for early board versions */
ZTEST(mica_keyboard, test_kb_init_early_board)
{
	fake_board_id = 0;
	zassert_equal(kb_init(), 0);
	zassert_equal(input_kbd_matrix_actual_key_mask_set_fake.call_count,
		      18 * 8);
}

/* Test keyboard initialization for late board versions */
ZTEST(mica_keyboard, test_kb_init_late_board)
{
	fake_board_id = 1;
	zassert_equal(kb_init(), 0);
	zassert_equal(input_kbd_matrix_actual_key_mask_set_fake.call_count, 0);
}

/* Test keyboard init error handling when setting key mask fails */
ZTEST(mica_keyboard, test_kb_init_mask_set_error)
{
	fake_board_id = 0;
	input_kbd_matrix_actual_key_mask_set_fake.return_val = -EINVAL;
	zassert_equal(kb_init(), 0);
	zassert_equal(input_kbd_matrix_actual_key_mask_set_fake.call_count,
		      18 * 8);
}
