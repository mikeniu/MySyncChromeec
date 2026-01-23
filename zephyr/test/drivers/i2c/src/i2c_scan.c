/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/ztest.h>

#define I2C4_NODE DT_NODELABEL(i2c4)
#define GET_I2C_ADDRESS(child_node) DT_REG_ADDR(child_node),

static const uint8_t i2c_addr_list[] = { DT_FOREACH_CHILD(I2C4_NODE,
							  GET_I2C_ADDRESS) };

static int verify_i2c_addr_match(uint8_t *scan, int size)
{
	int i, j;
	uint8_t mask;
	int match_cnt = 0;

	/*
	 * Verify that each i2c device address defined for i2c4 was found during
	 * the scan function.
	 */
	for (i = 0; i < ARRAY_SIZE(i2c_addr_list); i++) {
		mask = 1 << (i2c_addr_list[i] & 0x7);
		if (scan[i2c_addr_list[i] / 8] & mask) {
			match_cnt++;
		}
	}
	zassert_equal(match_cnt, ARRAY_SIZE(i2c_addr_list));

	/*
	 * Verify that only the expected number of devices were found during the
	 * i2c bus scan.
	 */
	match_cnt = 0;
	for (i = 0; i < size; i++) {
		for (j = 0; j < 8; j++) {
			if (scan[i] & (1 << j)) {
				match_cnt++;
			}
		}
	}
	zassert_equal(match_cnt, ARRAY_SIZE(i2c_addr_list));

	return 0;
}

ZTEST_USER(hc_ish_i2c_scan, test_hc_ish_i2c_scan_normal)
{
	struct ec_response_ish_i2c_scan response = { 0 };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_ISH_I2C_SCAN, UINT8_C(0), response);

	zassert_ok(host_command_process(&args));
	verify_i2c_addr_match((uint8_t *)args.response, args.response_size);
}

ZTEST_SUITE(hc_ish_i2c_scan, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
