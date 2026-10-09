/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_DRIVERS_COMMON_INCLUDE_TEST_DRIVERS_TEST_STATE_H_
#define PLATFORM_EC_ZEPHYR_TEST_DRIVERS_COMMON_INCLUDE_TEST_DRIVERS_TEST_STATE_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct test_state {
	bool ec_app_main_run;
};

bool drivers_predicate_pre_main(const void *state);

bool drivers_predicate_post_main(const void *state);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_TEST_DRIVERS_COMMON_INCLUDE_TEST_DRIVERS_TEST_STATE_H_ \
	*/
