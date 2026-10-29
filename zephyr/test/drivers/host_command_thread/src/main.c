/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file main.c
 *
 * WARNING:
 *   Do not add tests to this binary. This test messes with the main thread and
 *   can only run a single test function.
 */
#include "host_command.h"
#include "task.h"
#include "test/drivers/test_state.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define CUSTOM_COMMAND_ID 0x8888

/* Thread id of fake main thread */
static k_tid_t fake_main_tid;

/* 0 - did not run, 1 - true, -1 - false */
static int last_check_main_thread_result;
#if !defined(CONFIG_EC_HOST_CMD)
static int cmd_exec_delay_ms;
static K_SEM_DEFINE(cmd_done_sem, 0, 1);

static void test_send_response(struct host_cmd_handler_args *args)
{
	ARG_UNUSED(args);
	k_sem_give(&cmd_done_sem);
}
#endif

static enum ec_host_cmd_status
check_main_thread(struct ec_host_cmd_handler_args *args)
{
	last_check_main_thread_result =
		(k_current_get() == get_main_thread() ? 1 : -1);
#if !defined(CONFIG_EC_HOST_CMD)
	if (cmd_exec_delay_ms > 0)
		k_msleep(cmd_exec_delay_ms);
#endif
	return EC_HOST_CMD_SUCCESS;
}

EC_HOST_CMD_HANDLER_UNBOUND(CUSTOM_COMMAND_ID, check_main_thread,
			    EC_VER_MASK(0));

static void fake_main_thread(void *a, void *b, void *c)
{
	host_command_main();
}

K_THREAD_STACK_DEFINE(fake_main_thread_stack, 4000);

/* Override get_hostcmd_thread() from shim/src/tasks.c so
 * task_get_current() returns TASK_ID_HOSTCMD when fake main thread
 * is running.
 */
k_tid_t get_hostcmd_thread(void)
{
	return fake_main_tid;
}

ZTEST_SUITE(host_cmd_thread, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

ZTEST(host_cmd_thread, test_takeover)
{
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_SIMPLE(CUSTOM_COMMAND_ID, 0);
	const char expected_thread_name[] = "HOSTCMD";
	struct k_thread fake_main_thread_data;

	fake_main_tid = k_thread_create(
		&fake_main_thread_data, fake_main_thread_stack,
		K_THREAD_STACK_SIZEOF(fake_main_thread_stack), fake_main_thread,
		NULL, NULL, NULL, 1, 0, K_NO_WAIT);

	/* Wait for the thread to start */
	k_msleep(500);

	/* Get the name of the thread (must be done after the sleep) */
	const char *main_thread_name = k_thread_name_get(get_main_thread());

	/* Verify that the thread is not the hostcmd thread */
	zassert_equal(EC_TASK_PRIORITY(EC_TASK_HOSTCMD_PRIO),
		      k_thread_priority_get(get_main_thread()));
	zassert_equal(strlen(expected_thread_name), strlen(main_thread_name));
	zassert_mem_equal(expected_thread_name, main_thread_name,
			  strlen(expected_thread_name));

	/* Try running a host command */
	host_command_received(&args);
	k_msleep(1000);

	/* Make sure that the host command ran, the result should be -1 because
	 * it's not the original main thread.
	 */
	zassert_equal(-1, last_check_main_thread_result);

#if !defined(CONFIG_EC_HOST_CMD)
	/*
	 * Verify that back-to-back commands whose execution/preemption time
	 * exceeds CONFIG_HOSTCMD_RATE_LIMITING_MIN_REST (3ms) do not reset
	 * last_recess_time and trigger the 20ms rate-limiting recess once
	 * cumulative active time exceeds CONFIG_HOSTCMD_RATE_LIMITING_PERIOD
	 * (500ms).
	 */
	args.send_response = test_send_response;
	cmd_exec_delay_ms = 200;
	for (int i = 0; i < 3; i++) {
		int64_t t_start = k_uptime_get();
		int64_t elapsed;

		k_sem_reset(&cmd_done_sem);
		host_command_received(&args);
		zassert_ok(k_sem_take(&cmd_done_sem, K_MSEC(1000)));
		elapsed = k_uptime_get() - t_start;
		if (i == 2) {
			zassert_true(elapsed >= 220,
				     "Expected recess delay >= 220ms, got %lld",
				     elapsed);
		}
	}
	cmd_exec_delay_ms = 0;
#endif

	/* Kill the extra thread */
	k_thread_abort(fake_main_tid);
}
