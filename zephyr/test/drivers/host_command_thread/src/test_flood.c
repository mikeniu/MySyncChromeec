/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "task.h"
#include "test/drivers/test_state.h"

#include <zephyr/kernel.h>
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>
#include <zephyr/ztest.h>

#define CUSTOM_COMMAND_ID 0x8888

#ifdef CONFIG_EC_HOST_CMD
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
	int64_t start_time;
	int64_t elapsed;

	/* Ensure t_now - t_last > 3ms so t_recess resets to t_now */
	k_msleep(10);
	ec_host_cmd_user_cb(&rx_ctx, NULL);

	start_time = k_uptime_get();

	/*
	 * Simulate 255 back-to-back host commands spaced 2ms apart (<= 3ms
	 * MIN_REST). Total busy-wait is 510ms, which exceeds the 500ms PERIOD
	 * and must trigger the 20ms k_sleep() recess inside
	 * ec_host_cmd_user_cb.
	 */
	for (int i = 0; i < 255; i++) {
		k_busy_wait(2000);
		ec_host_cmd_user_cb(&rx_ctx, NULL);
	}

	elapsed = k_uptime_get() - start_time;
	zassert_true(elapsed >= 525,
		     "Expected >= 525ms with 20ms rate-limit sleep, got %lldms",
		     elapsed);
#else
	ztest_test_skip();
#endif
}
