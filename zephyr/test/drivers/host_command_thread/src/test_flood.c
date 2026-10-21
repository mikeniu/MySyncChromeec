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
	const int kInitialResetSleepMs = 10;
	const int kFloodCommandCount = 255;
	const int kBusyWaitUs = 2000;
	const int kExpectedRecessMs = 20;
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
	k_msleep(kInitialResetSleepMs);
	ec_host_cmd_user_cb(&rx_ctx, NULL);

	/*
	 * Simulate back-to-back host commands spaced 2ms apart (<= 3ms
	 * MIN_REST). Total busy-wait exceeds the 500ms PERIOD and must trigger
	 * exactly one 20ms k_sleep() recess inside ec_host_cmd_user_cb.
	 */
	for (int i = 0; i < kFloodCommandCount; i++) {
		k_busy_wait(kBusyWaitUs);
		call_start = k_uptime_get();
		ec_host_cmd_user_cb(&rx_ctx, NULL);
		recess_count +=
			(k_uptime_get() - call_start >= kExpectedRecessMs) ? 1 :
									     0;
	}

	zassert_equal(recess_count, 1,
		      "Expected exactly one %dms rate-limit recess, got %d",
		      kExpectedRecessMs, recess_count);
#else
	ztest_test_skip();
#endif
}

ZTEST(host_cmd_thread, test_host_command_flood_reset)
{
#ifdef CONFIG_EC_HOST_CMD
	const int kInitialResetSleepMs = 10;
	const int kResetCommandCount = 130;
	const int kResetBusyWaitUs = 4000;
	const int kExpectedRecessMs = 20;
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

	k_msleep(kInitialResetSleepMs);
	ec_host_cmd_user_cb(&rx_ctx, NULL);

	/*
	 * Space commands by 4ms (> 3ms MIN_REST) for 520ms total so t_recess
	 * resets on every command and no rate-limit recess is triggered.
	 */
	for (int i = 0; i < kResetCommandCount; i++) {
		k_busy_wait(kResetBusyWaitUs);
		call_start = k_uptime_get();
		ec_host_cmd_user_cb(&rx_ctx, NULL);
		recess_count +=
			(k_uptime_get() - call_start >= kExpectedRecessMs) ? 1 :
									     0;
	}

	zassert_equal(recess_count, 0,
		      "Expected 0 rate-limit recesses when spaced >3ms, got %d",
		      recess_count);
#else
	ztest_test_skip();
#endif
}

ZTEST(host_cmd_thread, test_host_command_flood_boundary)
{
#ifdef CONFIG_EC_HOST_CMD
	const int kInitialResetSleepMs = 10;
	const int kExactPeriodCommandCount = 250;
	const int kBusyWaitUs = 2000;
	const int kExpectedRecessMs = 20;
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

	k_msleep(kInitialResetSleepMs);
	ec_host_cmd_user_cb(&rx_ctx, NULL);

	/*
	 * Exactly 500ms (250 * 2ms) of sustained commands must NOT trigger a
	 * recess because the check is strictly `t_now - t_recess > 500`.
	 */
	for (int i = 0; i < kExactPeriodCommandCount; i++) {
		k_busy_wait(kBusyWaitUs);
		call_start = k_uptime_get();
		ec_host_cmd_user_cb(&rx_ctx, NULL);
		recess_count +=
			(k_uptime_get() - call_start >= kExpectedRecessMs) ? 1 :
									     0;
	}
	zassert_equal(recess_count, 0,
		      "Expected 0 recesses at exact 500ms boundary, got %d",
		      recess_count);

	/* Advancing 2ms past 500ms (502ms > 500ms) must trigger 1 recess. */
	k_busy_wait(kBusyWaitUs);
	call_start = k_uptime_get();
	ec_host_cmd_user_cb(&rx_ctx, NULL);
	recess_count +=
		(k_uptime_get() - call_start >= kExpectedRecessMs) ? 1 : 0;
	zassert_equal(recess_count, 1,
		      "Expected 1 recess immediately past 500ms boundary");
#else
	ztest_test_skip();
#endif
}
