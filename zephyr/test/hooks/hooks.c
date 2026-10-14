/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "hooks.h"
#include "timer.h"

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

static K_MUTEX_DEFINE(audit_test_mutex);
static K_SEM_DEFINE(audit_test_sem, 0, 1);
static K_CONDVAR_DEFINE(audit_test_condvar);
static K_EVENT_DEFINE(audit_test_event);
K_MSGQ_DEFINE(audit_test_msgq, sizeof(uint32_t), 2, 4);

static void audit_idle_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
}
static K_WORK_DEFINE(audit_idle_work, audit_idle_work_fn);

static bool non_blocking_deferred_done;

static void deferred_non_blocking_ops(void)
{
	uint32_t msg = 0x5a;

	zassert_ok(k_mutex_lock(&audit_test_mutex, K_NO_WAIT));
	(void)k_condvar_wait(&audit_test_condvar, &audit_test_mutex, K_NO_WAIT);
	zassert_ok(k_mutex_unlock(&audit_test_mutex));

	(void)k_sem_take(&audit_test_sem, K_NO_WAIT);
	(void)k_event_wait(&audit_test_event, 0x1, false, K_NO_WAIT);
	zassert_ok(k_msgq_put(&audit_test_msgq, &msg, K_NO_WAIT));
	zassert_ok(k_msgq_get(&audit_test_msgq, &msg, K_NO_WAIT));
	(void)k_thread_join(k_current_get(), K_NO_WAIT);

	non_blocking_deferred_done = true;
}
DECLARE_DEFERRED(deferred_non_blocking_ops);

static bool blocking_deferred_done;
static uint32_t blocking_deferred_violations;

static void deferred_blocking_ops(void)
{
	struct k_work_sync work_sync;
	uint32_t msg = 0xa5;

	sysworkq_audit_check_non_blocking("custom_check", NULL);
	crec_usleep(1); /* NOLINT_EC_SYMBOL */

	zassert_ok(k_mutex_lock(&audit_test_mutex, K_TICKS(1)));
	(void)k_condvar_wait(&audit_test_condvar, &audit_test_mutex,
			     K_TICKS(1));
	zassert_ok(k_mutex_unlock(&audit_test_mutex));

	k_sem_give(&audit_test_sem);
	zassert_ok(k_sem_take(&audit_test_sem, K_TICKS(1)));

	k_event_post(&audit_test_event, 0x1);
	(void)k_event_wait(&audit_test_event, 0x1, false, K_TICKS(1));

	zassert_ok(k_msgq_put(&audit_test_msgq, &msg, K_TICKS(1)));
	zassert_ok(k_msgq_get(&audit_test_msgq, &msg, K_TICKS(1)));

	(void)k_thread_join(k_current_get(), K_TICKS(1));
	(void)k_work_cancel_sync(&audit_idle_work, &work_sync);
	(void)k_work_flush(&audit_idle_work, &work_sync);

	blocking_deferred_violations = sysworkq_audit_get_violation_count();
	blocking_deferred_done = true;
}
DECLARE_DEFERRED(deferred_blocking_ops);

ZTEST(hooks_tests, test_sysworkq_runtime_check)
{
	/* Off sysworkq: blocking operations must not record violations */
	sysworkq_audit_reset_violation_count();
	sysworkq_audit_check_non_blocking("off_sysworkq", NULL);
	crec_usleep(1); /* NOLINT_EC_SYMBOL */
	zassert_ok(k_mutex_lock(&audit_test_mutex, K_TICKS(1)));
	zassert_ok(k_mutex_unlock(&audit_test_mutex));
	zassert_equal(sysworkq_audit_get_violation_count(), 0);

	/* On sysworkq with K_NO_WAIT: must not record violations */
	non_blocking_deferred_done = false;
	zassert_ok(hook_call_deferred(&deferred_non_blocking_ops_data, 0));
	k_usleep(DEFERRED_DELAY_US);
	zassert_true(non_blocking_deferred_done);
	zassert_equal(sysworkq_audit_get_violation_count(), 0);

	/* On sysworkq with blocking timeouts: each operation must flag */
	blocking_deferred_done = false;
	blocking_deferred_violations = 0;
	zassert_ok(hook_call_deferred(&deferred_blocking_ops_data, 0));
	k_usleep(DEFERRED_DELAY_US);
	zassert_true(blocking_deferred_done);
	/*
	 * 11 explicit blocking calls + 1 k_usleep inside crec_usleep +
	 * 1 internal k_mutex_lock(K_FOREVER) inside k_condvar_wait() = 13.
	 */
	zassert_equal(blocking_deferred_violations, 13);
	sysworkq_audit_reset_violation_count();
	zassert_equal(sysworkq_audit_get_violation_count(), 0);
}

static timestamp_t latency_fake_time;
static bool slow_hook_enabled;
static bool delayed_deferred_done;

static void slow_hook_fn(void)
{
	if (slow_hook_enabled) {
		latency_fake_time.val +=
			CONFIG_PLATFORM_EC_WORKQ_MAX_QUEUE_DELAY_US + 5000;
	}
}
DECLARE_HOOK(HOOK_TEST_3, slow_hook_fn, HOOK_PRIO_DEFAULT);

static void deferred_delayed_fn(void)
{
	delayed_deferred_done = true;
}
DECLARE_DEFERRED(deferred_delayed_fn);

static void deferred_staller_fn(void)
{
	zassert_ok(hook_call_deferred(&deferred_delayed_fn_data, 0));
	hook_notify(HOOK_TEST_3);
}
DECLARE_DEFERRED(deferred_staller_fn);

ZTEST(hooks_tests, test_workq_latency_audit)
{
	latency_fake_time.val = 1000;
	get_time_mock = &latency_fake_time;
	zephyr_shim_reset_workq_latency_warn_count();
	zassert_equal(zephyr_shim_get_workq_latency_warn_count(), 0);

	/* Fast hook and deferred calls on sysworkq must not warn */
	slow_hook_enabled = false;
	delayed_deferred_done = false;
	zassert_ok(hook_call_deferred(&deferred_staller_fn_data, 0));
	k_usleep(DEFERRED_DELAY_US);
	zassert_true(delayed_deferred_done);
	zassert_equal(zephyr_shim_get_workq_latency_warn_count(), 0);

	/*
	 * Trigger:
	 * 1. slow_hook_fn on sysworkq exceeds WORKQ_MAX_EXEC_US
	 * 2. deferred_staller_fn exceeds WORKQ_MAX_EXEC_US
	 * 3. deferred_delayed_fn exceeds WORKQ_MAX_QUEUE_DELAY_US
	 */
	slow_hook_enabled = true;
	delayed_deferred_done = false;
	zassert_ok(hook_call_deferred(&deferred_staller_fn_data, 0));
	k_usleep(DEFERRED_DELAY_US);
	zassert_true(delayed_deferred_done);
	zassert_equal(zephyr_shim_get_workq_latency_warn_count(), 3);

	slow_hook_enabled = false;
	get_time_mock = NULL;
	zephyr_shim_reset_workq_latency_warn_count();
	sysworkq_audit_reset_violation_count();
	zassert_equal(zephyr_shim_get_workq_latency_warn_count(), 0);
}

static bool sweep_done;

static void deferred_hook_sweep(void)
{
	static const enum hook_type sysworkq_hooks[] = {
		HOOK_INIT,
		HOOK_PRE_FREQ_CHANGE,
		HOOK_FREQ_CHANGE,
		HOOK_CHIPSET_PRE_INIT,
		HOOK_CHIPSET_STARTUP,
		HOOK_CHIPSET_RESUME,
		HOOK_CHIPSET_SUSPEND,
		HOOK_CHIPSET_SHUTDOWN,
		HOOK_CHIPSET_SHUTDOWN_COMPLETE,
		HOOK_CHIPSET_HARD_OFF,
		HOOK_CHIPSET_RESET,
		HOOK_AC_CHANGE,
		HOOK_LID_CHANGE,
		HOOK_TABLET_MODE_CHANGE,
		HOOK_BASE_ATTACHED_CHANGE,
		HOOK_POWER_BUTTON_CHANGE,
		HOOK_BATTERY_SOC_CHANGE,
		HOOK_TICK,
		HOOK_SECOND,
		HOOK_USB_PD_DISCONNECT,
		HOOK_USB_PD_CONNECT,
		HOOK_POWER_SUPPLY_CHANGE,
	};

	for (size_t i = 0; i < ARRAY_SIZE(sysworkq_hooks); i++) {
		hook_notify(sysworkq_hooks[i]);
	}
	sweep_done = true;
}
DECLARE_DEFERRED(deferred_hook_sweep);

ZTEST(hooks_tests, test_sysworkq_hook_sweep)
{
	sysworkq_audit_reset_violation_count();
	zephyr_shim_reset_workq_latency_warn_count();
	sweep_done = false;

	zassert_ok(hook_call_deferred(&deferred_hook_sweep_data, 0));
	k_usleep(DEFERRED_DELAY_US);

	zassert_true(sweep_done);
	zassert_equal(sysworkq_audit_get_violation_count(), 0);
	zassert_equal(zephyr_shim_get_workq_latency_warn_count(), 0);
}

ZTEST_SUITE(hooks_tests, NULL, NULL, NULL, NULL, NULL);
