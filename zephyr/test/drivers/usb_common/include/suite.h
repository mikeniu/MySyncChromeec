/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_DRIVERS_USB_COMMON_INCLUDE_SUITE_H_
#define PLATFORM_EC_ZEPHYR_TEST_DRIVERS_USB_COMMON_INCLUDE_SUITE_H_

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, board_vbus_source_enabled, int);
DECLARE_FAKE_VALUE_FUNC(int, ppc_discharge_vbus, int, int);

#endif /* PLATFORM_EC_ZEPHYR_TEST_DRIVERS_USB_COMMON_INCLUDE_SUITE_H_ */
