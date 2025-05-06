/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "port80.h"
#include "test/drivers/test_state.h"

#include <zephyr/drivers/espi_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

const struct device *const ESPI_DEV = DEVICE_DT_GET(DT_NODELABEL(espi0));

FAKE_VOID_FUNC(port_80_write, int);

ZTEST_USER(espi_shim, test_get_protocol_info)
{
	struct ec_response_get_protocol_info response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_PROTOCOL_INFO, 0, response);

	zassert_ok(host_command_process(&args));
}

ZTEST_USER(espi_shim, test_port80)
{
	emul_espi_host_port80_write(ESPI_DEV, 0x55aa);

	zassert_equal(port_80_write_fake.call_count, 1);
	zassert_equal(port_80_write_fake.arg0_val, 0x55aa);
}

ZTEST_SUITE(espi_shim, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
