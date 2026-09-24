/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_GERALT_INCLUDE_FAKES_H_
#define PLATFORM_EC_ZEPHYR_TEST_GERALT_INCLUDE_FAKES_H_

#include "battery.h"

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(enum battery_present, battery_is_present);

#endif /* PLATFORM_EC_ZEPHYR_TEST_GERALT_INCLUDE_FAKES_H_ */
