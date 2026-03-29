/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "acok_emul.h"
#include "extpower.h"
#include "hooks.h"
#include "host_command.h"
#include "lpc.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

ZTEST(extpower_init, test_extpower_init)
{
	/* Validate the initial state of the AC OK pin is captured by
	 * the extpower support.
	 */
	zassert_equal(extpower_is_present(), CONFIG_ACOK_INIT_VALUE);

	/* Verify that the host events are updated during initialization */
	if (IS_ENABLED(HAS_TASK_HOSTCMD)) {
		if (CONFIG_ACOK_INIT_VALUE) {
			zassert_true(
				host_is_event_set(EC_HOST_EVENT_AC_CONNECTED));
			zassert_false(host_is_event_set(
				EC_HOST_EVENT_AC_DISCONNECTED));
		} else {
			zassert_false(
				host_is_event_set(EC_HOST_EVENT_AC_CONNECTED));
			zassert_true(host_is_event_set(
				EC_HOST_EVENT_AC_DISCONNECTED));
		}
	}
}

ZTEST_SUITE(extpower_init, NULL, NULL, NULL, NULL, NULL);
