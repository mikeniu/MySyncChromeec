/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "drivers/cros_flash.h"
#include "emul/emul_flash.h"
#include "flash.h"
#include "host_command.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define CBI_FLASH_NODE DT_NODELABEL(cbi_flash)
#define CBI_FLASH_OFFSET DT_PROP(CBI_FLASH_NODE, offset)

#define cros_flash_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_flash_controller))

#define WP_L_GPIO_PATH NAMED_GPIOS_GPIO_NODE(wp_l)

static int gpio_wp_l_set(int value)
{
	const struct device *wp_l_gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(WP_L_GPIO_PATH, gpios));

	return gpio_emul_input_set(wp_l_gpio_dev,
				   DT_GPIO_PIN(WP_L_GPIO_PATH, gpios), value);
}

FAKE_VALUE_FUNC(int, crec_flash_unprotected_read, int, int, char *);

ZTEST(cbi_flash, test_cbi_flash_is_write_protected_when_locked)
{
	system_is_locked_fake.return_val = 1;
	zassert_equal(cbi_config->drv->is_protected(), 1);
	zassert_equal(system_is_locked_fake.call_count, 1);

	struct actual_set_params {
		struct ec_params_set_cbi_bin params;
		uint8_t actual_data[32];
	};

	struct actual_set_params hc_set_params = {
		.params = {
		.offset = 0,
		.size = 32,
		.flags = 0,
		},
	};
	struct host_cmd_handler_args set_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_CBI_BIN_WRITE, 0, hc_set_params);

	zassert_equal(host_command_process(&set_args), EC_RES_ACCESS_DENIED);
}

ZTEST(cbi_flash, test_cbi_flash_is_write_protected_when_ro_protected)
{
	cros_flash_init(cros_flash_dev);

	/* HW WP = 0, SW WP = 0 */
	gpio_wp_l_set(1);
	zassert_equal(cbi_config->drv->is_protected(), 0);

	/* HW WP = 1, SW WP = 0 */
	gpio_wp_l_set(0);
	zassert_equal(cbi_config->drv->is_protected(), 0);

	/* HW WP = 0, SW WP = 1 */
	gpio_wp_l_set(1);
	zassert_ok(crec_flash_physical_protect_now(false));
	zassert_equal(cbi_config->drv->is_protected(), 1);

	const uint8_t data[] = "SKU ABC";

	struct actual_set_params {
		struct ec_params_set_cbi params;
		uint8_t actual_data[ARRAY_SIZE(data)];
	};

	struct actual_set_params hc_set_params = {
		.params = {
		.tag = CBI_TAG_SKU_ID,
		/* Force a reload */
		.flag = CBI_SET_INIT,
		.size = ARRAY_SIZE(data),
		},
	};
	struct host_cmd_handler_args set_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_SET_CROS_BOARD_INFO, 0, hc_set_params);

	memcpy(hc_set_params.params.data, data, ARRAY_SIZE(data));

	zassert_equal(host_command_process(&set_args), EC_RES_ACCESS_DENIED);

	cros_flash_emul_protect_reset();
	gpio_wp_l_set(0);
}

ZTEST(cbi_flash, test_cbi_flash_is_write_protected_false)
{
	system_is_locked_fake.return_val = 0;
	zassert_equal(cbi_config->drv->is_protected(), 0);
	zassert_equal(system_is_locked_fake.call_count, 1);
}

ZTEST(cbi_flash, test_cbi_flash_load)
{
	int index;
	uint8_t input_data[CBI_IMAGE_SIZE];
	uint8_t data[CBI_IMAGE_SIZE];

	for (index = 0; index < CBI_IMAGE_SIZE; index++) {
		input_data[index] = index % 255;
	}
	cbi_config->drv->store(input_data);
	crec_flash_unprotected_read_fake.custom_fake = crec_flash_physical_read;

	zassert_ok(cbi_config->drv->load(0, data, CBI_IMAGE_SIZE));
	for (index = 0; index < CBI_IMAGE_SIZE; index++) {
		zassert_equal(data[index], index % 255);
	}

	zassert_ok(cbi_config->drv->load(211, data, CBI_IMAGE_SIZE - 211));
	for (index = 0; index < CBI_IMAGE_SIZE - 211; index++) {
		zassert_equal(data[index], (index + 211) % 255);
	}

	zassert_ok(cbi_config->drv->load(211, data, 0));
	for (index = 0; index < 0; index++) {
		zassert_equal(data[index], (index + 211) % 255);
	}

	zassert_equal(cbi_config->drv->load(0, data, -1), EC_ERROR_INVAL);

	zassert_equal(cbi_config->drv->load(-1, data, CBI_IMAGE_SIZE),
		      EC_ERROR_INVAL);

	zassert_equal(cbi_config->drv->load(0, data, CBI_IMAGE_SIZE + 1),
		      EC_ERROR_INVAL);

	zassert_equal(cbi_config->drv->load(1, data, CBI_IMAGE_SIZE),
		      EC_ERROR_INVAL);
}

ZTEST(cbi_flash, test_cbi_flash_load_error)
{
	uint8_t data[CBI_IMAGE_SIZE];

	crec_flash_unprotected_read_fake.return_val = EC_ERROR_INVAL;
	zassert_equal(cbi_config->drv->load(0, data, CBI_IMAGE_SIZE),
		      EC_ERROR_INVAL);
}

ZTEST(cbi_flash, test_cbi_flash_store)
{
	uint8_t data[CBI_IMAGE_SIZE];

	zassert_ok(cbi_config->drv->store(data));
}

ZTEST(cbi_flash, test_cbi_flash_store_fail)
{
	uint8_t data[CBI_IMAGE_SIZE];

	cros_flash_emul_enable_protect();
	zassert_equal(cbi_config->drv->store(data), EC_ERROR_ACCESS_DENIED);
	cros_flash_emul_protect_reset();
}

ZTEST(cbi_flash, test_cbi_flash_host_read_empty)
{
	struct ec_params_get_cbi_bin hc_get_params = {
		/* offset larger than image */
		.offset = CBI_FLASH_SIZE - 0x0100,
		.size = 32,
	};

	struct test_ec_params_get_cbi_response {
		uint8_t data[32];
	};
	struct test_ec_params_get_cbi_response hc_get_response;
	struct host_cmd_handler_args get_args = BUILD_HOST_COMMAND(
		EC_CMD_CBI_BIN_READ, 0, hc_get_response, hc_get_params);

	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_args.response_size, sizeof(hc_get_response));

	uint8_t response_is_empty[32];

	memset(response_is_empty, 0xFF, 32);

	zassert_mem_equal(hc_get_response.data, response_is_empty, 32);

	hc_get_params.offset = CBI_IMAGE_SIZE - 1;

	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_args.response_size, sizeof(hc_get_response));
}

ZTEST(cbi_flash, test_cbi_flash_host_write_overflow)
{
	struct actual_set_params {
		struct ec_params_set_cbi_bin params;
		uint8_t actual_data[32];
	};

	struct actual_set_params hc_set_params = {
		.params = {
		.offset = CBI_IMAGE_SIZE + 1, /* write buffer overflow */
		.size = 32,
		.flags = 0,
		},
	};
	struct host_cmd_handler_args set_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_CBI_BIN_WRITE, 0, hc_set_params);

	zassert_equal(host_command_process(&set_args), EC_RES_INVALID_PARAM);

	hc_set_params.params.offset = CBI_IMAGE_SIZE - 1;

	zassert_ok(host_command_process(&set_args));

	set_args.params_size = 0;

	zassert_equal(host_command_process(&set_args),
		      EC_HOST_CMD_REQUEST_TRUNCATED);
}

static void cbi_flash_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(crec_flash_unprotected_read);
}

ZTEST_SUITE(cbi_flash, drivers_predicate_post_main, NULL, cbi_flash_before,
	    NULL, NULL);
