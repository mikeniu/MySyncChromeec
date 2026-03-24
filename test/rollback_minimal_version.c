/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rollback.h"
#include "test_util.h"

/**
 * @brief Test that the current rollback minimal version matches the expected
 * value.
 *
 * This test verifies that the RO firmware has updated the rollback minimal
 * version in the rollback protection blocks to match the expected version
 * defined in the configuration.
 */
test_static int test_rollback_minimal_version(void)
{
	int32_t version;

	version = rollback_get_minimum_version();
	TEST_ASSERT(version >= 0);

	TEST_EQ(version, CONFIG_ROLLBACK_VERSION, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_rollback_minimal_version);

	test_print_result();
}
