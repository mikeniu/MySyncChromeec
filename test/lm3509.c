/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tests for TI LM3509 LED driver.
 */

#include "common.h"
#include "driver/led/lm3509.h"
#include "i2c.h"
#include "keyboard_backlight.h"
#include "test_util.h"

static int mock_bmain_reg = -1;

static int mock_lm3509_i2c_xfer(const int port, const uint16_t addr_flags,
				const uint8_t *out, int out_size, uint8_t *in,
				int in_size, int flags)
{
	if (port != I2C_PORT_KBLIGHT || addr_flags != LM3509_I2C_ADDR_FLAGS)
		return EC_ERROR_INVAL;

	if (out_size == 2 && in_size == 0 && out[0] == LM3509_REG_BMAIN) {
		mock_bmain_reg = out[1];
		return EC_SUCCESS;
	}

	return EC_ERROR_INVAL;
}
DECLARE_TEST_I2C_XFER(mock_lm3509_i2c_xfer);

static int test_set_brightness_boundaries(void)
{
	const int below_min_percent = -1;
	const int min_percent = 0;
	const int one_percent = 1;
	const int midpoint_tie_percent = 4;
	const int last_interval_round_down_percent = 93;
	const int last_interval_round_up_percent = 94;
	const int max_percent = 100;
	const int above_max_percent = 101;

	TEST_EQ(kblight_lm3509.set(below_min_percent), EC_SUCCESS, "%d");
	TEST_EQ(mock_bmain_reg, 0, "%d");

	TEST_EQ(kblight_lm3509.set(min_percent), EC_SUCCESS, "%d");
	TEST_EQ(mock_bmain_reg, 0, "%d");

	TEST_EQ(kblight_lm3509.set(one_percent), EC_SUCCESS, "%d");
	TEST_EQ(mock_bmain_reg, 3, "%d");

	/* Equidistant between index 11 (37) and index 12 (43) rounds up. */
	TEST_EQ(kblight_lm3509.set(midpoint_tie_percent), EC_SUCCESS, "%d");
	TEST_EQ(mock_bmain_reg, 12, "%d");

	/* Final interval between index 30 (875) and index 31 (1000). */
	TEST_EQ(kblight_lm3509.set(last_interval_round_down_percent),
		EC_SUCCESS, "%d");
	TEST_EQ(mock_bmain_reg, LM3509_BMAIN_MASK - 1, "%d");

	TEST_EQ(kblight_lm3509.set(last_interval_round_up_percent), EC_SUCCESS,
		"%d");
	TEST_EQ(mock_bmain_reg, LM3509_BMAIN_MASK, "%d");

	TEST_EQ(kblight_lm3509.set(max_percent), EC_SUCCESS, "%d");
	TEST_EQ(mock_bmain_reg, LM3509_BMAIN_MASK, "%d");

	/* Out-of-range (> 100%) clamps to the highest table index (31). */
	TEST_EQ(kblight_lm3509.set(above_max_percent), EC_SUCCESS, "%d");
	TEST_EQ(mock_bmain_reg, LM3509_BMAIN_MASK, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	RUN_TEST(test_set_brightness_boundaries);
	test_print_result();
}
