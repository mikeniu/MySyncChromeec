/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/rt1718s.h"
#include "emul/tcpc/emul_rt1718s.h"
#include "gpio.h"
#include "test/drivers/test_state.h"
#include "test_common.h"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(rt1718s_gpio, drivers_predicate_post_main, NULL,
	    rt1718s_clear_set_reg_history, rt1718s_clear_set_reg_history, NULL);

ZTEST(rt1718s_gpio, test_set_gpio_flags)
{
	struct {
		int gpio_config;
		int expected_reg_val;
	} testdata[] = {
		{
			/* output high with open drain */
			GPIO_OUTPUT | GPIO_HIGH | GPIO_OPEN_DRAIN,
			RT1718S_GPIO_CTRL_OE | RT1718S_GPIO_CTRL_O,
		},
		{
			/* output high with push pull  */
			GPIO_OUTPUT | GPIO_HIGH,
			RT1718S_GPIO_CTRL_OE | RT1718S_GPIO_CTRL_O |
				RT1718S_GPIO_CTRL_OD_N,
		},
		{
			/* output low with open drain */
			GPIO_OUTPUT | GPIO_LOW | GPIO_OPEN_DRAIN,
			RT1718S_GPIO_CTRL_OE,
		},
		{
			/* output low with push pull */
			GPIO_OUTPUT | GPIO_LOW,
			RT1718S_GPIO_CTRL_OE | RT1718S_GPIO_CTRL_OD_N,
		},
		{
			/* input with floating */
			GPIO_INPUT,
			RT1718S_GPIO_CTRL_OD_N,
		},
		{
			/* input with pull up */
			GPIO_INPUT | GPIO_PULL_UP,
			RT1718S_GPIO_CTRL_PU | RT1718S_GPIO_CTRL_OD_N,
		},
		{
			/* input with pull down */
			GPIO_INPUT | GPIO_PULL_DOWN,
			RT1718S_GPIO_CTRL_PD | RT1718S_GPIO_CTRL_OD_N,
		},
		{
			/* input with pull up and down */
			GPIO_INPUT | GPIO_PULL_UP | GPIO_PULL_DOWN,
			RT1718S_GPIO_CTRL_PU | RT1718S_GPIO_CTRL_PD |
				RT1718S_GPIO_CTRL_OD_N,
		},
	};

	for (int gpio_num = RT1718S_GPIO1; gpio_num < RT1718S_GPIO_COUNT;
	     gpio_num++) {
		for (int i = 0; i < ARRAY_SIZE(testdata); i++) {
			rt1718s_gpio_set_flags(tcpm_rt1718s_port, gpio_num,
					       testdata[i].gpio_config);
			compare_reg_val_with_mask(rt1718s_emul,
						  RT1718S_GPIO_CTRL(gpio_num),
						  testdata[i].expected_reg_val,
						  0xFF);
		}
	}
}

ZTEST(rt1718s_gpio, test_set_level)
{
	for (int gpio_num = RT1718S_GPIO1; gpio_num < RT1718S_GPIO_COUNT;
	     gpio_num++) {
		rt1718s_gpio_set_level(tcpm_rt1718s_port, gpio_num, 0);
		compare_reg_val_with_mask(rt1718s_emul,
					  RT1718S_GPIO_CTRL(gpio_num), 0,
					  RT1718S_GPIO_CTRL_O);
		rt1718s_gpio_set_level(tcpm_rt1718s_port, gpio_num, 1);
		compare_reg_val_with_mask(rt1718s_emul,
					  RT1718S_GPIO_CTRL(gpio_num), 0xFF,
					  RT1718S_GPIO_CTRL_O);
	}
}

ZTEST(rt1718s_gpio, test_get_level)
{
	for (int gpio_num = RT1718S_GPIO1; gpio_num < RT1718S_GPIO_COUNT;
	     gpio_num++) {
		zassert_ok(rt1718s_emul_set_reg(
			rt1718s_emul, RT1718S_GPIO_CTRL(gpio_num), 0));
		zassert_equal(
			rt1718s_gpio_get_level(tcpm_rt1718s_port, gpio_num), 0);
		zassert_ok(rt1718s_emul_set_reg(rt1718s_emul,
						RT1718S_GPIO_CTRL(gpio_num),
						RT1718S_GPIO_CTRL_I));
		zassert_equal(
			rt1718s_gpio_get_level(tcpm_rt1718s_port, gpio_num), 1);
	}
}

ZTEST(rt1718s_gpio, test_command_rt1718s_gpio)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "rt1718s_gpio"),
		   "Cannot execute command to get gpio on rt1718s");
}

struct gpio_sysworkq_test_data {
	struct k_work work;
	struct k_sem done;
	int level;
};

static struct gpio_sysworkq_test_data test_gpio_data;

static void gpio_sysworkq_test_work_handler(struct k_work *work)
{
	struct gpio_sysworkq_test_data *data =
		CONTAINER_OF(work, struct gpio_sysworkq_test_data, work);

	rt1718s_gpio_set_level(tcpm_rt1718s_port, RT1718S_GPIO1, 1);
	data->level = rt1718s_gpio_get_level(tcpm_rt1718s_port, RT1718S_GPIO1);

	k_sem_give(&data->done);
}

ZTEST(rt1718s_gpio, test_gpio_sysworkq)
{
	struct k_work_sync sync;

	k_sem_init(&test_gpio_data.done, 0, 1);
	k_work_init(&test_gpio_data.work, gpio_sysworkq_test_work_handler);

	/* Set up GPIO1 as output high in emulator */
	zassert_ok(rt1718s_emul_set_reg(rt1718s_emul,
					RT1718S_GPIO_CTRL(RT1718S_GPIO1),
					RT1718S_GPIO_CTRL_I));

	k_work_submit(&test_gpio_data.work);

	zassert_equal(k_sem_take(&test_gpio_data.done, K_MSEC(50)), 0,
		      "GPIO operation did not complete in time on sysworkq");
	zassert_equal(test_gpio_data.level, 1, "Expected GPIO level 1");

	k_work_flush(&test_gpio_data.work, &sync);
}
