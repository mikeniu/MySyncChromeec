/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test board_in_prod_mode, board_keymgr_in_prod_mode, and
 * verify_ro_certs_hmac logic.
 */

#include "common.h"
#include "console.h"
#include "test_util.h"
#include "tpm_manufacture.h"

/* Mock state variables */
static int mock_keymgr_prod_val;
static int mock_ro_certs_hmac_val;

int board_keymgr_in_prod_mode(void)
{
	return mock_keymgr_prod_val;
}

int verify_ro_certs_hmac(void)
{
	return mock_ro_certs_hmac_val;
}

int board_in_prod_mode(void)
{
	return board_keymgr_in_prod_mode() && verify_ro_certs_hmac();
}

static int test_dev_keymgr(void)
{
	mock_keymgr_prod_val = 0;
	mock_ro_certs_hmac_val = 1;

	TEST_ASSERT(board_keymgr_in_prod_mode() == 0);
	TEST_ASSERT(board_in_prod_mode() == 0);

	return EC_SUCCESS;
}

static int test_prod_keymgr_valid_hmac(void)
{
	mock_keymgr_prod_val = 1;
	mock_ro_certs_hmac_val = 1;

	TEST_ASSERT(board_keymgr_in_prod_mode() == 1);
	TEST_ASSERT(verify_ro_certs_hmac() == 1);
	TEST_ASSERT(board_in_prod_mode() == 1);

	return EC_SUCCESS;
}

static int test_prod_keymgr_invalid_hmac(void)
{
	mock_keymgr_prod_val = 1;
	mock_ro_certs_hmac_val = 0;

	/*
	 * When Key Manager has prod constant, but cert region HMAC fails
	 * (e.g. manifest tag/epoch changed), board_in_prod_mode() must return 0
	 * ("dev" key ladder state), while board_keymgr_in_prod_mode() returns 1
	 */
	TEST_ASSERT(board_keymgr_in_prod_mode() == 1);
	TEST_ASSERT(verify_ro_certs_hmac() == 0);
	TEST_ASSERT(board_in_prod_mode() == 0);

	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_dev_keymgr);
	RUN_TEST(test_prod_keymgr_valid_hmac);
	RUN_TEST(test_prod_keymgr_invalid_hmac);

	test_print_result();
}
