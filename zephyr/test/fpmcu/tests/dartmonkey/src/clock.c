/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "module_id.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <stm32_ll_pwr.h>
#include <stm32_ll_rcc.h>
#include <stm32_ll_utils.h>

FAKE_VOID_FUNC(LL_PWR_SetRegulVoltageScaling, uint32_t);
FAKE_VALUE_FUNC(uint32_t, LL_PWR_IsActiveFlag_VOS);
FAKE_VOID_FUNC(LL_RCC_PLL1_Enable);
FAKE_VALUE_FUNC(uint32_t, LL_RCC_PLL1_IsReady);
FAKE_VOID_FUNC(LL_RCC_PLL1_Disable);
FAKE_VOID_FUNC(LL_RCC_SetAHBPrescaler, uint32_t);
FAKE_VOID_FUNC(LL_RCC_SetAPB1Prescaler, uint32_t);
FAKE_VOID_FUNC(LL_RCC_SetAPB2Prescaler, uint32_t);
FAKE_VOID_FUNC(LL_RCC_SetAPB3Prescaler, uint32_t);
FAKE_VOID_FUNC(LL_RCC_SetAPB4Prescaler, uint32_t);
FAKE_VOID_FUNC(LL_SetFlashLatency, uint32_t);
FAKE_VOID_FUNC(LL_RCC_SetSysClkSource, uint32_t);
FAKE_VALUE_FUNC(uint32_t, LL_RCC_GetSysClkSource);
FAKE_VALUE_FUNC(uint32_t, LL_RCC_HSI_IsReady);
FAKE_VOID_FUNC(LL_RCC_HSI_Enable);
FAKE_VOID_FUNC(LL_SetSystemCoreClock, uint32_t);

#define EXPECTED_HSI_FREQ 64000000U
#define EXPECTED_PLL_FREQ 400000000U
#define EXPECTED_HCLK_PLL_FREQ (EXPECTED_PLL_FREQ / 2U)
#define EXPECTED_HCLK_HSI_FREQ (EXPECTED_HSI_FREQ / 1U)

static void reset_all_fakes(void)
{
	RESET_FAKE(LL_PWR_SetRegulVoltageScaling);
	RESET_FAKE(LL_PWR_IsActiveFlag_VOS);
	RESET_FAKE(LL_RCC_PLL1_Enable);
	RESET_FAKE(LL_RCC_PLL1_IsReady);
	RESET_FAKE(LL_RCC_PLL1_Disable);
	RESET_FAKE(LL_RCC_SetAHBPrescaler);
	RESET_FAKE(LL_RCC_SetAPB1Prescaler);
	RESET_FAKE(LL_RCC_SetAPB2Prescaler);
	RESET_FAKE(LL_RCC_SetAPB3Prescaler);
	RESET_FAKE(LL_RCC_SetAPB4Prescaler);
	RESET_FAKE(LL_SetFlashLatency);
	RESET_FAKE(LL_RCC_SetSysClkSource);
	RESET_FAKE(LL_RCC_GetSysClkSource);
	RESET_FAKE(LL_RCC_HSI_IsReady);
	RESET_FAKE(LL_RCC_HSI_Enable);
	RESET_FAKE(LL_SetSystemCoreClock);
}

static void clock_enable_module_fast_cpu_fake_retvals_set(int enable)
{
	if (enable) {
		LL_PWR_IsActiveFlag_VOS_fake.return_val = 1;
		LL_RCC_PLL1_IsReady_fake.return_val = 1;
		LL_RCC_GetSysClkSource_fake.return_val =
			LL_RCC_SYS_CLKSOURCE_STATUS_PLL1;
		LL_RCC_HSI_IsReady_fake.return_val = 1;
	} else {
		LL_RCC_HSI_IsReady_fake.return_val = 1;
		LL_RCC_GetSysClkSource_fake.return_val =
			LL_RCC_SYS_CLKSOURCE_STATUS_HSI;
		LL_PWR_IsActiveFlag_VOS_fake.return_val = 1;
	}
}

static void clock_test_before(void *fixture)
{
	ARG_UNUSED(fixture);

	reset_all_fakes();

	/* Ensure clock is in low-speed (HSI) state before each test */
	clock_enable_module_fast_cpu_fake_retvals_set(0);
	clock_enable_module(MODULE_FAST_CPU, 0);

	reset_all_fakes();
}

static void clock_test_after(void *fixture)
{
	ARG_UNUSED(fixture);

	reset_all_fakes();

	/* Clean up back to disabled state */
	clock_enable_module_fast_cpu_fake_retvals_set(0);
	clock_enable_module(MODULE_FAST_CPU, 0);
}

ZTEST_SUITE(stm32_clock, NULL, NULL, clock_test_before, clock_test_after, NULL);

ZTEST(stm32_clock, test_clock_enable_module_non_fast_cpu)
{
	/* Non-FAST_CPU modules should be ignored */
	clock_enable_module(MODULE_ADC, 1);
	clock_enable_module(MODULE_I2C, 0);
	clock_enable_module(MODULE_SPI, 1);

	zassert_equal(LL_PWR_SetRegulVoltageScaling_fake.call_count, 0);
	zassert_equal(LL_RCC_PLL1_Enable_fake.call_count, 0);
	zassert_equal(LL_RCC_PLL1_Disable_fake.call_count, 0);
	zassert_equal(LL_RCC_SetSysClkSource_fake.call_count, 0);
}

ZTEST(stm32_clock, test_clock_enable_fast_cpu)
{
	uint32_t vos_seq[] = { 0, 1 };
	uint32_t pll_ready_seq[] = { 0, 1 };
	uint32_t sysclk_seq[] = { LL_RCC_SYS_CLKSOURCE_STATUS_HSI,
				  LL_RCC_SYS_CLKSOURCE_STATUS_PLL1 };

	SET_RETURN_SEQ(LL_PWR_IsActiveFlag_VOS, vos_seq, 2);
	SET_RETURN_SEQ(LL_RCC_PLL1_IsReady, pll_ready_seq, 2);
	SET_RETURN_SEQ(LL_RCC_GetSysClkSource, sysclk_seq, 2);

	clock_enable_module_fast_cpu_fake_retvals_set(1);
	clock_enable_module(MODULE_FAST_CPU, 1);

	/* Verify voltage scaling to VOS1 */
	zassert_equal(LL_PWR_SetRegulVoltageScaling_fake.call_count, 1);
	zassert_equal(LL_PWR_SetRegulVoltageScaling_fake.arg0_history[0],
		      LL_PWR_REGU_VOLTAGE_SCALE1);
	zassert_equal(LL_PWR_IsActiveFlag_VOS_fake.call_count, 2);

	/* Verify PLL1 enabled and waited for */
	zassert_equal(LL_RCC_PLL1_Enable_fake.call_count, 1);
	zassert_equal(LL_RCC_PLL1_IsReady_fake.call_count, 2);

	/* Verify AHB prescaler set to DIV_2 (200 MHz) */
	zassert_equal(LL_RCC_SetAHBPrescaler_fake.call_count, 1);
	zassert_equal(LL_RCC_SetAHBPrescaler_fake.arg0_history[0],
		      LL_RCC_AHB_DIV_2);

	/* Verify APB prescalers set to DIV_2 */
	zassert_equal(LL_RCC_SetAPB1Prescaler_fake.call_count, 1);
	zassert_equal(LL_RCC_SetAPB1Prescaler_fake.arg0_history[0],
		      LL_RCC_APB1_DIV_2);
	zassert_equal(LL_RCC_SetAPB2Prescaler_fake.call_count, 1);
	zassert_equal(LL_RCC_SetAPB2Prescaler_fake.arg0_history[0],
		      LL_RCC_APB2_DIV_2);
	zassert_equal(LL_RCC_SetAPB3Prescaler_fake.call_count, 1);
	zassert_equal(LL_RCC_SetAPB3Prescaler_fake.arg0_history[0],
		      LL_RCC_APB3_DIV_2);
	zassert_equal(LL_RCC_SetAPB4Prescaler_fake.call_count, 1);
	zassert_equal(LL_RCC_SetAPB4Prescaler_fake.arg0_history[0],
		      LL_RCC_APB4_DIV_2);

	/* Verify flash latency updated for 200 MHz */
	zassert_equal(LL_SetFlashLatency_fake.call_count, 1);
	zassert_equal(LL_SetFlashLatency_fake.arg0_history[0],
		      EXPECTED_HCLK_PLL_FREQ);

	/* Verify SYSCLK switched to PLL1 */
	zassert_equal(LL_RCC_SetSysClkSource_fake.call_count, 1);
	zassert_equal(LL_RCC_SetSysClkSource_fake.arg0_history[0],
		      LL_RCC_SYS_CLKSOURCE_PLL1);
	zassert_equal(LL_RCC_GetSysClkSource_fake.call_count, 2);

	/* Verify SystemCoreClock updated */
	zassert_equal(LL_SetSystemCoreClock_fake.call_count, 1);
	zassert_equal(LL_SetSystemCoreClock_fake.arg0_history[0],
		      EXPECTED_PLL_FREQ);
}

ZTEST(stm32_clock, test_clock_enable_fast_cpu_already_enabled)
{
	/* Enable fast CPU first */
	clock_enable_module_fast_cpu_fake_retvals_set(1);
	clock_enable_module(MODULE_FAST_CPU, 1);

	reset_all_fakes();

	/* Enable again when already enabled - should be a no-op */
	clock_enable_module(MODULE_FAST_CPU, 1);

	zassert_equal(LL_PWR_SetRegulVoltageScaling_fake.call_count, 0);
	zassert_equal(LL_RCC_PLL1_Enable_fake.call_count, 0);
	zassert_equal(LL_RCC_SetAHBPrescaler_fake.call_count, 0);
	zassert_equal(LL_RCC_SetSysClkSource_fake.call_count, 0);
}

ZTEST(stm32_clock, test_clock_disable_fast_cpu_hsi_not_ready)
{
	uint32_t hsi_ready_seq[] = { 0, 0, 1 };
	uint32_t sysclk_seq[] = { LL_RCC_SYS_CLKSOURCE_STATUS_PLL1,
				  LL_RCC_SYS_CLKSOURCE_STATUS_HSI };
	uint32_t vos_seq[] = { 0, 1 };

	/* Enable fast CPU first */
	clock_enable_module_fast_cpu_fake_retvals_set(1);
	clock_enable_module(MODULE_FAST_CPU, 1);
	reset_all_fakes();

	/* HSI is not ready initially */
	SET_RETURN_SEQ(LL_RCC_HSI_IsReady, hsi_ready_seq, 3);
	SET_RETURN_SEQ(LL_RCC_GetSysClkSource, sysclk_seq, 2);
	SET_RETURN_SEQ(LL_PWR_IsActiveFlag_VOS, vos_seq, 2);

	clock_enable_module(MODULE_FAST_CPU, 0);

	/* Verify HSI enabled and waited for */
	zassert_equal(LL_RCC_HSI_Enable_fake.call_count, 1);
	zassert_equal(LL_RCC_HSI_IsReady_fake.call_count, 3);

	/* Verify SYSCLK switched to HSI */
	zassert_equal(LL_RCC_SetSysClkSource_fake.call_count, 1);
	zassert_equal(LL_RCC_SetSysClkSource_fake.arg0_history[0],
		      LL_RCC_SYS_CLKSOURCE_HSI);
	zassert_equal(LL_RCC_GetSysClkSource_fake.call_count, 2);

	/* Verify AHB prescaler set to DIV_1 (64 MHz) */
	zassert_equal(LL_RCC_SetAHBPrescaler_fake.call_count, 1);
	zassert_equal(LL_RCC_SetAHBPrescaler_fake.arg0_history[0],
		      LL_RCC_AHB_DIV_1);

	/* Verify APB prescalers set to DIV_1 */
	zassert_equal(LL_RCC_SetAPB1Prescaler_fake.call_count, 1);
	zassert_equal(LL_RCC_SetAPB1Prescaler_fake.arg0_history[0],
		      LL_RCC_APB1_DIV_1);
	zassert_equal(LL_RCC_SetAPB2Prescaler_fake.call_count, 1);
	zassert_equal(LL_RCC_SetAPB2Prescaler_fake.arg0_history[0],
		      LL_RCC_APB2_DIV_1);
	zassert_equal(LL_RCC_SetAPB3Prescaler_fake.call_count, 1);
	zassert_equal(LL_RCC_SetAPB3Prescaler_fake.arg0_history[0],
		      LL_RCC_APB3_DIV_1);
	zassert_equal(LL_RCC_SetAPB4Prescaler_fake.call_count, 1);
	zassert_equal(LL_RCC_SetAPB4Prescaler_fake.arg0_history[0],
		      LL_RCC_APB4_DIV_1);

	/* Verify flash latency updated for 64 MHz */
	zassert_equal(LL_SetFlashLatency_fake.call_count, 1);
	zassert_equal(LL_SetFlashLatency_fake.arg0_history[0],
		      EXPECTED_HCLK_HSI_FREQ);

	/* Verify PLL1 disabled */
	zassert_equal(LL_RCC_PLL1_Disable_fake.call_count, 1);

	/* Verify voltage scaling down to VOS3 */
	zassert_equal(LL_PWR_SetRegulVoltageScaling_fake.call_count, 1);
	zassert_equal(LL_PWR_SetRegulVoltageScaling_fake.arg0_history[0],
		      LL_PWR_REGU_VOLTAGE_SCALE3);
	zassert_equal(LL_PWR_IsActiveFlag_VOS_fake.call_count, 2);

	/* Verify SystemCoreClock updated */
	zassert_equal(LL_SetSystemCoreClock_fake.call_count, 1);
	zassert_equal(LL_SetSystemCoreClock_fake.arg0_history[0],
		      EXPECTED_HSI_FREQ);
}

ZTEST(stm32_clock, test_clock_disable_fast_cpu_hsi_already_ready)
{
	/* Enable fast CPU first */
	clock_enable_module_fast_cpu_fake_retvals_set(1);
	clock_enable_module(MODULE_FAST_CPU, 1);
	reset_all_fakes();

	/* HSI is already ready */
	clock_enable_module_fast_cpu_fake_retvals_set(0);
	clock_enable_module(MODULE_FAST_CPU, 0);

	/* HSI_Enable should not be called since HSI was already ready */
	zassert_equal(LL_RCC_HSI_Enable_fake.call_count, 0);
	zassert_equal(LL_RCC_HSI_IsReady_fake.call_count, 1);

	/* PLL1 should still be disabled */
	zassert_equal(LL_RCC_PLL1_Disable_fake.call_count, 1);

	/* Voltage scaling should still be set to VOS3 */
	zassert_equal(LL_PWR_SetRegulVoltageScaling_fake.call_count, 1);
	zassert_equal(LL_PWR_SetRegulVoltageScaling_fake.arg0_history[0],
		      LL_PWR_REGU_VOLTAGE_SCALE3);

	zassert_equal(LL_SetSystemCoreClock_fake.call_count, 1);
	zassert_equal(LL_SetSystemCoreClock_fake.arg0_history[0],
		      EXPECTED_HSI_FREQ);
}

ZTEST(stm32_clock, test_clock_disable_fast_cpu_already_disabled)
{
	/* Disable again when already disabled - should be a no-op */
	clock_enable_module(MODULE_FAST_CPU, 0);

	zassert_equal(LL_RCC_HSI_Enable_fake.call_count, 0);
	zassert_equal(LL_RCC_SetSysClkSource_fake.call_count, 0);
	zassert_equal(LL_RCC_PLL1_Disable_fake.call_count, 0);
	zassert_equal(LL_PWR_SetRegulVoltageScaling_fake.call_count, 0);
}
