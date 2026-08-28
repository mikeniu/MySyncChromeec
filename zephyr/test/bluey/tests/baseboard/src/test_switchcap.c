/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "gpio.h"
#include "power/qcom.h"
#include "stubs.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

void board_set_switchcap_power(int enable);
int board_is_switchcap_enabled(void);
int board_is_switchcap_power_good(void);
int board_is_switchcap_power_reset(void);

static void test_before(void *fixture)
{
	RESET_FAKE(adc_read_channel);
}

ZTEST_SUITE(bluey_baseboard_switchcap, NULL, NULL, test_before, NULL, NULL);

/* Test enabling and disabling switchcap power rail */
ZTEST(bluey_baseboard_switchcap, test_switchcap_enable_disable)
{
	board_set_switchcap_power(1);
	zassert_equal(board_is_switchcap_enabled(), 1,
		      "Switchcap should be enabled");

	board_set_switchcap_power(0);
	zassert_equal(board_is_switchcap_enabled(), 0,
		      "Switchcap should be disabled");
}

/* Test switchcap power good signal check */
ZTEST(bluey_baseboard_switchcap, test_switchcap_power_good)
{
	/* Power good threshold is 2000 mV */
	adc_read_channel_fake.return_val = 2600;
	zassert_equal(board_is_switchcap_power_good(), 1,
		      "2600mV should be power good");

	adc_read_channel_fake.return_val = 2001;
	zassert_equal(board_is_switchcap_power_good(), 1,
		      "2001mV should be power good");

	adc_read_channel_fake.return_val = 2000;
	zassert_equal(board_is_switchcap_power_good(), 0,
		      "2000mV should not be power good");

	adc_read_channel_fake.return_val = 1500;
	zassert_equal(board_is_switchcap_power_good(), 0,
		      "1500mV should not be power good");
}

/* Test switchcap power reset logic */
ZTEST(bluey_baseboard_switchcap, test_switchcap_power_reset)
{
	/* Reset threshold is 10 mV */
	adc_read_channel_fake.return_val = 0;
	zassert_equal(board_is_switchcap_power_reset(), 1,
		      "0mV should be power reset");

	adc_read_channel_fake.return_val = 10;
	zassert_equal(board_is_switchcap_power_reset(), 1,
		      "10mV should be power reset");

	adc_read_channel_fake.return_val = 11;
	zassert_equal(board_is_switchcap_power_reset(), 0,
		      "11mV should not be power reset");

	adc_read_channel_fake.return_val = 2600;
	zassert_equal(board_is_switchcap_power_reset(), 0,
		      "2600mV should not be power reset");
}
