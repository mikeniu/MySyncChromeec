/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_CHARGER_INCLUDE_CHARGER_TEST_H_
#define PLATFORM_EC_ZEPHYR_TEST_CHARGER_INCLUDE_CHARGER_TEST_H_

#include <stdbool.h>

bool charger_predicate_pre_main(const void *state);
bool charger_predicate_post_main(const void *state);

#endif /* PLATFORM_EC_ZEPHYR_TEST_CHARGER_INCLUDE_CHARGER_TEST_H_ */
