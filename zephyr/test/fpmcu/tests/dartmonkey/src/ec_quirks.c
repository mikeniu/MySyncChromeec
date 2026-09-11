/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/dt-bindings/reset/stm32h7_reset.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(arm_core_mpu_disable);
FAKE_VALUE_FUNC(int, stm32_reset_line_toggle, const struct device *, uint32_t);

static struct reset_driver_api stm32_reset_api = {
	.line_toggle = stm32_reset_line_toggle,
};

DEVICE_DT_DEFINE(DT_NODELABEL(rctl), NULL, NULL, NULL, NULL, PRE_KERNEL_1, 0,
		 &stm32_reset_api);

static void *ec_quirks_setup(void)
{
	RESET_FAKE(arm_core_mpu_disable);
	RESET_FAKE(stm32_reset_line_toggle);

	return NULL;
}

ZTEST_SUITE(ec_quirks, NULL, ec_quirks_setup, NULL, NULL, NULL);

ZTEST(ec_quirks, test_prepare_for_sysjump)
{
	hook_notify(HOOK_SYSJUMP);

	zassert_equal(arm_core_mpu_disable_fake.call_count, 1,
		      "MPU should be disabled before sysjump");

	zassert_equal(stm32_reset_line_toggle_fake.call_count, 2,
		      "Timers and UARTs should be reset");

	zassert_equal(stm32_reset_line_toggle_fake.arg0_history[0],
		      DEVICE_DT_GET(DT_NODELABEL(rctl)));
	zassert_equal(stm32_reset_line_toggle_fake.arg1_history[0],
		      STM32_RESET(APB1L, 0U),
		      "TIM2 reset line toggle mismatch");

	zassert_equal(stm32_reset_line_toggle_fake.arg0_history[1],
		      DEVICE_DT_GET(DT_NODELABEL(rctl)));
	zassert_equal(stm32_reset_line_toggle_fake.arg1_history[1],
		      STM32_RESET(APB2, 4U),
		      "USART1 reset line toggle mismatch");
}
