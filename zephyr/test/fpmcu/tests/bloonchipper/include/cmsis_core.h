/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_FPMCU_TESTS_BLOONCHIPPER_INCLUDE_CMSIS_CORE_H_
#define PLATFORM_EC_ZEPHYR_TEST_FPMCU_TESTS_BLOONCHIPPER_INCLUDE_CMSIS_CORE_H_

#include <stdint.h>

static inline void __disable_irq(void)
{
}

static inline void __set_BASEPRI(uint32_t basePri)
{
	(void)basePri;
}

static inline void __DSB(void)
{
}

static inline void __ISB(void)
{
}

#endif /* PLATFORM_EC_ZEPHYR_TEST_FPMCU_TESTS_BLOONCHIPPER_INCLUDE_CMSIS_CORE_H_ \
	*/
