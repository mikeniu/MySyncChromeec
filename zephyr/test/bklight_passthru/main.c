/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "backlight.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#define GPIO_LID_OPEN_PATH DT_PATH(named_gpios, lid_open_ec)
#define GPIO_LID_OPEN_PIN DT_GPIO_PIN(GPIO_LID_OPEN_PATH, gpios)

#define GPIO_ENABLE_BKLIGHT_PATH DT_PATH(named_gpios, enable_backlight)
#define GPIO_ENABLE_BKLIGHT_PIN DT_GPIO_PIN(GPIO_ENABLE_BKLIGHT_PATH, gpios)

static const struct device *gpio_dev;

static void set_lid_state(int is_open)
{
	gpio_emul_input_set(gpio_dev, GPIO_LID_OPEN_PIN, is_open);
	k_msleep(100);
}

static int get_backlight_en(void)
{
	return gpio_emul_output_get(gpio_dev, GPIO_ENABLE_BKLIGHT_PIN);
}

static int send_bklight_hostcmd(int enabled)
{
	struct ec_params_switch_enable_backlight p = {
		.enabled = enabled,
	};
	struct host_cmd_handler_args args = {
		.command = EC_CMD_SWITCH_ENABLE_BKLIGHT,
		.version = 0,
		.params = &p,
		.params_size = sizeof(p),
	};

	return host_command_process(&args);
}

static void *bklight_passthru_setup(void)
{
	gpio_dev = DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_LID_OPEN_PATH, gpios));
	hook_notify(HOOK_INIT);
	return NULL;
}

ZTEST_SUITE(bklight_passthru, NULL, bklight_passthru_setup, NULL, NULL, NULL);

ZTEST(bklight_passthru, test_passthrough)
{
	/* Open lid -> backlight turns on */
	set_lid_state(1);
	zassert_equal(1, lid_is_open(), "lid_is_open() is %d", lid_is_open());
	zassert_equal(1, get_backlight_en());

	/* Close lid -> backlight turns off */
	set_lid_state(0);
	zassert_equal(0, lid_is_open(), "lid_is_open() is %d", lid_is_open());
	zassert_equal(0, get_backlight_en());

	/* Open lid again -> backlight turns on */
	set_lid_state(1);
	zassert_equal(1, get_backlight_en());
}

ZTEST(bklight_passthru, test_hostcommand)
{
	/* Open lid -> backlight turns on */
	set_lid_state(1);
	zassert_equal(1, get_backlight_en());

	/* Disable by host command -> backlight turns off */
	zassert_ok(send_bklight_hostcmd(0));
	zassert_equal(0, get_backlight_en());

	/* Close and open lid -> backlight turns back on */
	set_lid_state(0);
	set_lid_state(1);
	zassert_equal(1, get_backlight_en());

	/* Enable by host command while open */
	zassert_ok(send_bklight_hostcmd(1));
	zassert_equal(1, get_backlight_en());

	/* Disable backlight by lid */
	set_lid_state(0);
	zassert_equal(0, get_backlight_en());
}
