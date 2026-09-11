/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/policy.h>
#include <zephyr/ztest.h>

static uint32_t hook_chip_resume_cnt;
static uint32_t hook_chip_suspend_cnt;

int is_board_prod_hw(bool *is_prod)
{
	/* just a stub */
	*is_prod = true;
	return 0;
}

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(substate_id);
	ARG_UNUSED(state);
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);
	irq_unlock(0);
}

static void chipset_resume(void)
{
	hook_chip_resume_cnt++;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, chipset_resume, HOOK_PRIO_DEFAULT);

static void chipset_suspend(void)
{
	hook_chip_suspend_cnt++;
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, chipset_suspend, HOOK_PRIO_DEFAULT);

static void *power_setup(void)
{
	return NULL;
}

ZTEST_SUITE(power, NULL, power_setup, NULL, NULL, NULL);

ZTEST(power, test_slp_event)
{
	const struct device *slp_l_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(slp_l), gpios));
	const gpio_port_pins_t slp_l_pin =
		DT_GPIO_PIN(DT_NODELABEL(slp_l), gpios);
	const struct device *slp_alt_l_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(slp_alt_l), gpios));
	const gpio_port_pins_t slp_alt_l_pin =
		DT_GPIO_PIN(DT_NODELABEL(slp_alt_l), gpios);

	/* Set init state: both 0 (suspend) */
	gpio_emul_input_set(slp_alt_l_gpio, slp_alt_l_pin, 0);
	gpio_emul_input_set(slp_l_gpio, slp_l_pin, 0);
	k_sleep(K_MSEC(100));
	hook_chip_suspend_cnt = 0;
	hook_chip_resume_cnt = 0;

	/* Set slp_alt_l = 1, slp_l = 0 (still suspend) */
	gpio_emul_input_set(slp_alt_l_gpio, slp_alt_l_pin, 1);
	k_sleep(K_MSEC(100));
	zassert_equal(
		hook_chip_suspend_cnt, 1,
		"Suspend hook should be called when slp_alt_l is toggled while slp_l is 0");
	zassert_equal(hook_chip_resume_cnt, 0,
		      "Resume hook should not be called yet");
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      0, "PM lock should not be active in suspend");

	/* Set AP S0: both slp_alt_l = 1 and slp_l = 1 */
	gpio_emul_input_set(slp_l_gpio, slp_l_pin, 1);
	k_sleep(K_MSEC(100));
	zassert_equal(hook_chip_suspend_cnt, 1,
		      "Suspend hook count should not change");
	zassert_equal(hook_chip_resume_cnt, 1,
		      "Resume hook should be called on transition to S0");
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      1, "PM lock should be active in S0");

	/* Suspend via slp_l = 0 (S0ix) */
	gpio_emul_input_set(slp_l_gpio, slp_l_pin, 0);
	k_sleep(K_MSEC(100));
	zassert_equal(hook_chip_suspend_cnt, 2,
		      "Suspend hook should be called on transition to S0ix");
	zassert_equal(hook_chip_resume_cnt, 1);
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      0, "PM lock should be released in S0ix");

	/* Redundant suspend trigger (slp_alt_l = 0 while slp_l = 0) */
	gpio_emul_input_set(slp_alt_l_gpio, slp_alt_l_pin, 0);
	k_sleep(K_MSEC(100));
	zassert_equal(hook_chip_suspend_cnt, 3,
		      "Suspend hook should be called again");
	zassert_equal(hook_chip_resume_cnt, 1);
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      0, "PM lock should remain inactive");

	/* Set slp_l = 1 while slp_alt_l = 0 (S3 state) */
	gpio_emul_input_set(slp_l_gpio, slp_l_pin, 1);
	k_sleep(K_MSEC(100));
	zassert_equal(hook_chip_suspend_cnt, 4,
		      "Suspend hook should be called in S3 state");
	zassert_equal(hook_chip_resume_cnt, 1);
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      0, "PM lock should remain inactive in S3");

	/* Resume back to S0: set slp_alt_l = 1 */
	gpio_emul_input_set(slp_alt_l_gpio, slp_alt_l_pin, 1);
	k_sleep(K_MSEC(100));
	zassert_equal(hook_chip_suspend_cnt, 4);
	zassert_equal(hook_chip_resume_cnt, 2,
		      "Resume hook should be called on return to S0");
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      1, "PM lock should be active in S0");

	/* Suspend back via slp_alt_l = 0 */
	gpio_emul_input_set(slp_alt_l_gpio, slp_alt_l_pin, 0);
	k_sleep(K_MSEC(100));
	zassert_equal(hook_chip_suspend_cnt, 5);
	zassert_equal(hook_chip_resume_cnt, 2);
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      0, "PM lock should be released");
}
