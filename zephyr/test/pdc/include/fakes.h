/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_PDC_INCLUDE_FAKES_H_
#define PLATFORM_EC_ZEPHYR_TEST_PDC_INCLUDE_FAKES_H_

#include "battery.h"

#include <zephyr/fff.h>

void set_battery_present(enum battery_present bp);

extern int mock_usb_pd_3a_ports;

#endif /* PLATFORM_EC_ZEPHYR_TEST_PDC_INCLUDE_FAKES_H_ */
