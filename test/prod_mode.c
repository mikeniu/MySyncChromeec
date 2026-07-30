/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test is_keymgr_prod_mode and compute_board_in_prod_mode helpers.
 */

#include "common.h"
#include "console.h"
#include "test_util.h"
#include "tpm_manufacture.h"

static int test_is_keymgr_prod_mode(void)
{
	/* Success case */
	TEST_ASSERT(is_keymgr_prod_mode(0, 0xaa66150f) == true);

	/* Bad FWR7 */
	TEST_ASSERT(is_keymgr_prod_mode(1, 0xaa66150f) == false);

	/* Bad RWR7 */
	TEST_ASSERT(is_keymgr_prod_mode(0, 0xaa660000) == false);

	/* Both bad */
	TEST_ASSERT(is_keymgr_prod_mode(1, 0xaa660000) == false);

	return EC_SUCCESS;
}

static int test_compute_board_in_prod_mode(void)
{
	/* Both true */
	TEST_ASSERT(compute_board_in_prod_mode(true, true) == true);

	/* Keymgr false */
	TEST_ASSERT(compute_board_in_prod_mode(false, true) == false);

	/* HMAC false */
	TEST_ASSERT(compute_board_in_prod_mode(true, false) == false);

	/* Both false */
	TEST_ASSERT(compute_board_in_prod_mode(false, false) == false);

	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_is_keymgr_prod_mode);
	RUN_TEST(test_compute_board_in_prod_mode);

	test_print_result();
}
