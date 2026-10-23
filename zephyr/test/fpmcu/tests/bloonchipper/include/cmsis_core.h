/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_FPMCU_TESTS_BLOONCHIPPER_INCLUDE_CMSIS_CORE_H_
#define PLATFORM_EC_ZEPHYR_TEST_FPMCU_TESTS_BLOONCHIPPER_INCLUDE_CMSIS_CORE_H_

#include <stdint.h>

#include <zephyr/fff.h>

DECLARE_FAKE_VOID_FUNC(__disable_irq);
DECLARE_FAKE_VOID_FUNC(__set_BASEPRI, uint32_t);
DECLARE_FAKE_VOID_FUNC(__DSB);
DECLARE_FAKE_VOID_FUNC(__ISB);

#endif /* PLATFORM_EC_ZEPHYR_TEST_FPMCU_TESTS_BLOONCHIPPER_INCLUDE_CMSIS_CORE_H_ \
	*/
