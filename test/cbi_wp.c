/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test CBI EEPROM WP
 */

#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "ec_commands.h"
#include "gpio.h"
#include "host_command.h"
#include "system.h"
#include "test_util.h"
#include "util.h"

static int system_locked;

static void test_setup(void)
{
	/* Make sure that write protect is disabled */
	gpio_set_level(GPIO_WP, 0);
	gpio_set_level(GPIO_EC_CBI_WP, 0);
	system_locked = 0;
}

static void test_teardown(void)
{
}

int system_is_locked(void)
{
	return system_locked;
}

DECLARE_EC_TEST(test_wp)
{
	int cbi_wp;
	uint32_t val = 0x12345678;

	cbi_wp = gpio_get_level(GPIO_EC_CBI_WP);
	zassert_equal(cbi_wp, 0);
	zassert_equal(cbi_config->drv->is_protected(), 0);

	zassert_ok(cbi_create());
	zassert_ok(cbi_set_board_info(CBI_TAG_SKU_ID, (uint8_t *)&val,
				      sizeof(val)));
	zassert_ok(cbi_write());

	cbi_latch_eeprom_wp();
	cbi_wp = gpio_get_level(GPIO_EC_CBI_WP);
	zassert_equal(cbi_wp, 1);
	zassert_equal(cbi_config->drv->is_protected(), 1);

	zassert_equal(cbi_write(), EC_ERROR_ACCESS_DENIED);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_wp_blocks_bin_write)
{
	uint32_t original_sku = 0x11223344;
	uint32_t read_sku;
	uint8_t size = sizeof(read_sku);

	gpio_set_level(GPIO_EC_CBI_WP, 0);
	zassert_equal(cbi_config->drv->is_protected(), 0);
	zassert_ok(cbi_clear());
	zassert_ok(cbi_create());
	zassert_ok(cbi_set_board_info(CBI_TAG_SKU_ID, (uint8_t *)&original_sku,
				      sizeof(original_sku)));

	cbi_latch_eeprom_wp();
	zassert_equal(cbi_config->drv->is_protected(), 1);

	struct actual_set_params {
		struct ec_params_set_cbi_bin params;
		uint8_t actual_data[4];
	} __packed;

	struct actual_set_params hc_set_params = {
		.params = {
			.offset = 0,
			.size = sizeof(original_sku),
			.flags = EC_CBI_BIN_BUFFER_CLEAR,
		},
	};

	struct host_cmd_handler_args set_args = {
		.version = 0,
		.command = EC_CMD_CBI_BIN_WRITE,
		.params = &hc_set_params,
		.params_size = sizeof(hc_set_params),
		.response = NULL,
		.response_max = 0,
		.response_size = 0,
	};

	zassert_equal(host_command_process(&set_args), EC_RES_ACCESS_DENIED);

	zassert_ok(cbi_get_board_info(CBI_TAG_SKU_ID, (uint8_t *)&read_sku,
				      &size));
	zassert_equal(read_sku, original_sku);

	gpio_set_level(GPIO_EC_CBI_WP, 0);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_wp_blocks_set)
{
	uint32_t original_sku = 0x55667788;
	uint32_t new_sku = 0x99AABBCC;
	uint32_t read_sku;
	uint8_t size = sizeof(read_sku);

	gpio_set_level(GPIO_EC_CBI_WP, 0);
	zassert_ok(cbi_clear());
	zassert_ok(cbi_create());
	zassert_ok(cbi_set_board_info(CBI_TAG_SKU_ID, (uint8_t *)&original_sku,
				      sizeof(original_sku)));

	cbi_latch_eeprom_wp();

	struct actual_set_params {
		struct ec_params_set_cbi params;
		uint8_t actual_data[sizeof(new_sku)];
	} __packed;

	struct actual_set_params hc_set_params = {
		.params = {
			.tag = CBI_TAG_SKU_ID,
			.flag = 0,
			.size = sizeof(new_sku),
		},
	};
	memcpy(hc_set_params.params.data, &new_sku, sizeof(new_sku));

	struct host_cmd_handler_args set_args = {
		.version = 0,
		.command = EC_CMD_SET_CROS_BOARD_INFO,
		.params = &hc_set_params,
		.params_size = sizeof(hc_set_params),
		.response = NULL,
		.response_max = 0,
		.response_size = 0,
	};

	zassert_equal(host_command_process(&set_args), EC_RES_ACCESS_DENIED);

	zassert_ok(cbi_get_board_info(CBI_TAG_SKU_ID, (uint8_t *)&read_sku,
				      &size));
	zassert_equal(read_sku, original_sku);

	gpio_set_level(GPIO_EC_CBI_WP, 0);

	return EC_SUCCESS;
}

TEST_SUITE(test_suite_cbi_wp)
{
	ztest_test_suite(
		test_cbi_wp,
		ztest_unit_test_setup_teardown(test_wp, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_wp_blocks_bin_write,
					       test_setup, test_teardown),
		ztest_unit_test_setup_teardown(test_wp_blocks_set, test_setup,
					       test_teardown));
	ztest_run_test_suite(test_cbi_wp);
}
