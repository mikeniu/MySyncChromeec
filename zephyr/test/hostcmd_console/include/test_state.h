/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_HOSTCMD_CONSOLE_INCLUDE_TEST_STATE_H_
#define PLATFORM_EC_ZEPHYR_TEST_HOSTCMD_CONSOLE_INCLUDE_TEST_STATE_H_

#include <stdbool.h>

struct test_state {
	bool ec_app_main_run;
};

bool predicate_post_main(const void *state);

#endif /* PLATFORM_EC_ZEPHYR_TEST_HOSTCMD_CONSOLE_INCLUDE_TEST_STATE_H_ */
