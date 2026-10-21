/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "task.h"
#include "test/drivers/test_state.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define CUSTOM_COMMAND_ID 0x8888

#ifdef CONFIG_EC_HOST_CMD
/* Conflicts with host_command_legacy.h when CONFIG_EC_HOST_CMD is off. */
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>

void ec_host_cmd_user_cb(const struct ec_host_cmd_rx_ctx *rx_ctx,
			 void *user_data);
#endif

ZTEST(host_cmd_thread, test_host_command_flood)
{
#ifdef CONFIG_EC_HOST_CMD
	struct ec_host_cmd_request_header hdr = {
		.prtcl_ver = 3,
		.cmd_id = CUSTOM_COMMAND_ID,
	};
	struct ec_host_cmd_rx_ctx rx_ctx = {
		.buf = (uint8_t *)&hdr,
		.len = sizeof(hdr),
	};
	int64_t call_start;
	int recess_count = 0;

	/* Ensure t_now - t_last > 3ms so t_recess resets to t_now */
	k_msleep(10);
	ec_host_cmd_user_cb(&rx_ctx, NULL);

	/*
	 * Simulate 255 back-to-back host commands spaced 2ms apart (<= 3ms
	 * MIN_REST). Total busy-wait is 510ms, which exceeds the 500ms PERIOD
	 * and must trigger exactly one 20ms k_sleep() recess inside
	 * ec_host_cmd_user_cb. Time each call individually so loop overhead
	 * cannot produce a false pass.
	 */
	for (int i = 0; i < 255; i++) {
		k_busy_wait(2000);
		call_start = k_uptime_get();
		ec_host_cmd_user_cb(&rx_ctx, NULL);
		if (k_uptime_get() - call_start >= 20)
			recess_count++;
	}

	zassert_equal(recess_count, 1,
		      "Expected exactly one 20ms rate-limit recess, got %d",
		      recess_count);
#else
	ztest_test_skip();
#endif
}
