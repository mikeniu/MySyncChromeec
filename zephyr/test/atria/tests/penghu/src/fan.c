/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>

ZTEST_SUITE(penghu_fan, NULL, NULL, NULL, NULL, NULL);

ZTEST(penghu_fan, test_fan_dummy)
{
	zassert_true(true, "Dummy fan test passed");
}
