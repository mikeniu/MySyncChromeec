<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "test_state.h"

#include <zephyr/drivers/emul_stub_device.h>
#include <zephyr/ztest.h>

ZTEST(host_cmd, test_hibernate_get)
{
	struct ec_response_hibernation_delay response;
	struct ec_params_hibernation_delay params = {
		.seconds = 0,
	};

	zassert_ok(ec_cmd_hibernation_delay(NULL, &params, &response));
	params.seconds = 123;
	zassert_ok(ec_cmd_hibernation_delay(NULL, &params, &response));
	zassert_equal(123, response.hibernate_delay, NULL);
}

ZTEST_SUITE(host_cmd, ap_power_predicate_post_main, NULL, NULL, NULL, NULL);

/* These 2 lines are needed because we don't define an espi host driver */
#define DT_DRV_COMPAT zephyr_espi_emul_espi_host
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "emul/emul_stub_device.h"
#include "host_command.h"
#include "test_state.h"

#include <zephyr/ztest.h>

ZTEST(host_cmd, test_hibernate_get)
{
	struct ec_response_hibernation_delay response;
	struct ec_params_hibernation_delay params = {
		.seconds = 0,
	};

	zassert_ok(ec_cmd_hibernation_delay(NULL, &params, &response));
	params.seconds = 123;
	zassert_ok(ec_cmd_hibernation_delay(NULL, &params, &response));
	zassert_equal(123, response.hibernate_delay, NULL);
}

ZTEST_SUITE(host_cmd, ap_power_predicate_post_main, NULL, NULL, NULL, NULL);

/* These 2 lines are needed because we don't define an espi host driver */
#define DT_DRV_COMPAT zephyr_espi_emul_espi_host
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
