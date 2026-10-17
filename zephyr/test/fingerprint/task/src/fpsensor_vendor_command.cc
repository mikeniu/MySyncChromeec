/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "system.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor.h>
#include <fpsensor/fpsensor_state_driver.h>
#include <fpsensor/fpsensor_utils.h>
#include <fpsensor_driver.h>
#include <host_command.h>
#include <mkbp_event.h>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

namespace
{

constexpr uint32_t kFpVendorParam1 = 0xabcd;
int fp_vendor_command_custom_ret;
int is_locked;
bool overflow_response;

int run_vendor_cmd(uint32_t param1 = kFpVendorParam1)
{
	ec_params_fp_vendor params{
		.param1 = param1,
	};
	return ec_cmd_fp_vendor(nullptr, &params);
}

void reset(void *data)
{
	ARG_UNUSED(data);

	is_locked = 0;
	overflow_response = false;
	fp_vendor_command_custom_ret = 0;
}

} /* namespace */

int system_is_locked()
{
	return is_locked;
}

int fp_vendor_command(uint32_t param, uint8_t *buf, size_t buf_size)
{
	zassert_equal(param, kFpVendorParam1);

	if (overflow_response) {
		return buf_size + 1;
	}
	return fp_vendor_command_custom_ret;
}

ZTEST(hc_fp_vendor, test_fp_vendor_ok)
{
	zassert_equal(EC_RES_SUCCESS, run_vendor_cmd());
}

ZTEST(hc_fp_vendor, test_fp_vendor_custom_not_ok)
{
	fp_vendor_command_custom_ret = -1;
	zassert_equal(EC_RES_ERROR, run_vendor_cmd());
}

ZTEST(hc_fp_vendor, test_fp_vendor_locked)
{
	is_locked = 1;
	zassert_equal(EC_RES_ACCESS_DENIED, run_vendor_cmd());
}

ZTEST(hc_fp_vendor, test_fp_vendor_overflow)
{
	overflow_response = true;
	zassert_equal(EC_RES_RESPONSE_TOO_BIG, run_vendor_cmd());
}

ZTEST_SUITE(hc_fp_vendor, nullptr, nullptr, reset, reset, nullptr);
