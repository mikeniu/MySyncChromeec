/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_DRIVERS_USBC_POWER_CONTRACT_INCLUDE_MOCK_USBC_POWER_CONTRACT_H_
#define PLATFORM_EC_ZEPHYR_TEST_DRIVERS_USBC_POWER_CONTRACT_INCLUDE_MOCK_USBC_POWER_CONTRACT_H_

#include <stdint.h>

#include <zephyr/fff.h>

/* FFF fake declarations for select functions in `usbc_power_contract.c` */
DECLARE_FAKE_VALUE_FUNC(int, dpm_get_source_pdo, const uint32_t, const int);

#endif /* PLATFORM_EC_ZEPHYR_TEST_DRIVERS_USBC_POWER_CONTRACT_INCLUDE_MOCK_USBC_POWER_CONTRACT_H_ \
	*/
