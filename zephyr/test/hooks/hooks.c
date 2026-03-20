/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "hooks.h"

#include <stdbool.h>

#include <zephyr/ztest.h>

static bool h1_called;
static bool h2_called;
static bool h3_called;

static void h1(void)
{
	zassert_false(h1_called, "h1 was called, but should not have been");
	zassert_false(h2_called, "h2 was called, but should not have been");
	zassert_false(h3_called, "h3 was called, but should not have been");
	h1_called = true;
}
DECLARE_HOOK(HOOK_TEST_1, h1, HOOK_PRIO_FIRST);

static void h2(void)
{
	zassert_true(h1_called, "h1 was not called, but should have been");
	zassert_false(h2_called, "h2 was called, but should not have been");
	zassert_false(h3_called, "h3 was called, but should not have been");
	h2_called = true;
}
DECLARE_HOOK(HOOK_TEST_1, h2, HOOK_PRIO_DEFAULT);

static void h3(void)
{
	zassert_true(h1_called, "h1 was not called, but should have been");
	zassert_true(h2_called, "h2 was not called, but should have been");
	zassert_false(h3_called, "h3 was called, but should not have been");
	h3_called = true;
}
DECLARE_HOOK(HOOK_TEST_1, h3, HOOK_PRIO_LAST);

ZTEST(hooks_tests, test_hook_list_multiple)
{
	hook_notify(HOOK_TEST_1);
	zassert_true(h1_called, "h1 was not called, but should have been");
	zassert_true(h2_called, "h2 was not called, but should have been");
	zassert_true(h3_called, "h3 was not called, but should have been");
}

static bool h4_called;

static void h4(void)
{
	zassert_false(h4_called, "h4 was called, but should not have been");
	h4_called = true;
}
DECLARE_HOOK(HOOK_TEST_2, h4, HOOK_PRIO_DEFAULT);

ZTEST(hooks_tests, test_hook_list_single)
{
	hook_notify(HOOK_TEST_2);
	zassert_true(h4_called, "h4 was not called, but should have been");
}

ZTEST(hooks_tests, test_hook_list_empty)
{
	hook_notify(HOOK_TEST_3);
}

static bool deferred_func_called;

#define DEFERRED_DELAY_US (500 * 1000)
static void deferred_func(void)
{
	deferred_func_called = true;
}
DECLARE_DEFERRED(deferred_func);

ZTEST(hooks_tests, test_deferred_func)
{
	zassert_false(
		deferred_func_called,
		"The deferred function was called, but should not have been");
	hook_call_deferred(&deferred_func_data, DEFERRED_DELAY_US);
	zassert_false(
		deferred_func_called,
		"The deferred function was called, but should not have been");
	k_usleep(DEFERRED_DELAY_US * 2);
	zassert_true(
		deferred_func_called,
		"The deferred function was not called, but should have been");
}

static bool deferred_func_2_called;

static void deferred_func_2(void)
{
	deferred_func_2_called = true;
}
DECLARE_DEFERRED(deferred_func_2);

/*
 * Test that repeated calls to hook_call_deferred result in the
 * function being pushed out.
 */
ZTEST(hooks_tests, test_deferred_func_push_out)
{
	zassert_false(
		deferred_func_2_called,
		"The deferred function was called, but should not have been");
	hook_call_deferred(&deferred_func_2_data, DEFERRED_DELAY_US);
	hook_call_deferred(&deferred_func_2_data, DEFERRED_DELAY_US * 3);
	k_usleep(DEFERRED_DELAY_US * 2);
	zassert_false(
		deferred_func_2_called,
		"The deferred function was called, but should not have been");
	k_usleep(DEFERRED_DELAY_US * 2);
	zassert_true(
		deferred_func_called,
		"The deferred function was not called, but should have been");
}

static bool deferred_func_3_called;

static void deferred_func_3(void)
{
	deferred_func_3_called = true;
}
DECLARE_DEFERRED(deferred_func_3);

ZTEST(hooks_tests, test_deferred_func_cancel)
{
	zassert_false(
		deferred_func_3_called,
		"The deferred function was called, but should not have been");
	hook_call_deferred(&deferred_func_3_data, DEFERRED_DELAY_US);
	hook_call_deferred(&deferred_func_3_data, -1);
	k_usleep(DEFERRED_DELAY_US * 2);
	zassert_false(
		deferred_func_3_called,
		"The deferred function was called, but should not have been");
}

static void deferred_cancels_and_reschedules_self(void);
DECLARE_DEFERRED(deferred_cancels_and_reschedules_self);

static bool cancelled_and_rescheduled_ok;

static void deferred_cancels_and_reschedules_self(void)
{
	static bool executed;

	if (!executed) {
		executed = true;
		/*
		 * Cancelling this task while it's running puts it in CANCELING
		 * state which causes k_work_reschedule to return an error if
		 * the delay is K_NO_WAIT.
		 */
		zassert_ok(hook_call_deferred(
			&deferred_cancels_and_reschedules_self_data, -1));

		/*
		 * Run this again with a value that becomes K_NO_WAIT if we're
		 * not careful.
		 */
		const int reschedule_delay = 0;

		zassert_true(
			K_TIMEOUT_EQ(K_NO_WAIT, K_USEC(reschedule_delay)),
			"Delay for rescheduling must translate to K_NO_WAIT for"
			" this test to operate as intended.");
		zassert_ok(hook_call_deferred(
			&deferred_cancels_and_reschedules_self_data,
			reschedule_delay));
	} else {
		cancelled_and_rescheduled_ok = true;
	}
}

ZTEST(hooks_tests, test_deferred_avoids_k_no_wait)
{
	zassert_ok(hook_call_deferred(
		&deferred_cancels_and_reschedules_self_data, 0));
	k_usleep(2 * DEFERRED_DELAY_US);

	zassert_true(cancelled_and_rescheduled_ok);
}

/*
 * Shared state for AP power event tests.
 */
static int ev_count;
static enum ap_power_events ev_last_event;

static void ev_handler(struct ap_power_ev_callback *cb,
		       struct ap_power_ev_data data)
{
	ev_count++;
	ev_last_event = data.event;
}
AP_POWER_EVENT_CALLBACK_DEFINE(ev_handler, AP_POWER_SUSPEND, AP_POWER_RESUME,
			       AP_POWER_STARTUP);

ZTEST(hooks_tests, test_hook_ap_power_events)
{
	ev_count = 0;
	ev_last_event = 0;
	hook_notify(HOOK_CHIPSET_SUSPEND);
	zassert_equal(1, ev_count, "Callback not called");
	zassert_equal(AP_POWER_SUSPEND, ev_last_event, "Wrong event");

	hook_notify(HOOK_CHIPSET_RESUME);
	zassert_equal(2, ev_count, "Callback not called for RESUME");
	zassert_equal(AP_POWER_RESUME, ev_last_event, "Wrong event");

	/* Verify unregistered event does not fire */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	zassert_equal(2, ev_count, "Callback called for unregistered event");

	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(3, ev_count, "Startup callback not called");
}

ZTEST_SUITE(hooks_tests, NULL, NULL, NULL, NULL, NULL);
