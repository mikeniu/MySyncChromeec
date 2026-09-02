/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_FPMCU_TESTS_BLOONCHIPPER_INCLUDE_STM32F4XX_LL_TIM_H_
#define PLATFORM_EC_ZEPHYR_TEST_FPMCU_TESTS_BLOONCHIPPER_INCLUDE_STM32F4XX_LL_TIM_H_

/* Add definitions of types normally provided by the STM32 HAL.
 * Tests don't use HAL, all needed functions are mocked.
 */
typedef uint32_t TIM_TypeDef;
void LL_TIM_DisableCounter(TIM_TypeDef *TIMx);

#endif /* PLATFORM_EC_ZEPHYR_TEST_FPMCU_TESTS_BLOONCHIPPER_INCLUDE_STM32F4XX_LL_TIM_H_ \
	*/
