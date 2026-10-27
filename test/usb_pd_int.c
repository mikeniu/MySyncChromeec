/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB-PD interrupt task.
 */
#include "hooks.h"
#include "mock/tcpc_mock.h"
#include "mock/timer_mock.h"
#include "mock/usb_mux_mock.h"
#include "task.h"
#include "test_util.h"

#define PORT0 0

/* Install Mock TCPC and MUX drivers */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.drv = &mock_tcpc_driver,
	},
};

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = { {
	.mux =
		&(const struct usb_mux){
			.driver = &mock_usb_mux_driver,
		},
} };

void board_reset_pd_mcu(void)
{
}

static int deferred_resume_called;
void pd_deferred_resume(int port)
{
	deferred_resume_called = 1;
}

static int num_events;
uint16_t tcpc_get_alert_status(void)
{
	if (--num_events > 0)
		return PD_STATUS_TCPC_ALERT_0;
	else
		return 0;
}

test_static int test_storm_not_triggered(void)
{
	num_events = 100;
	deferred_resume_called = 0;
	schedule_deferred_pd_interrupt(PORT0);
	task_wait_event(SECOND);
	TEST_EQ(deferred_resume_called, 0, "%d");

	return EC_SUCCESS;
}

test_static int test_storm_triggered(void)
{
	num_events = 2000;
	deferred_resume_called = 0;
	schedule_deferred_pd_interrupt(PORT0);
	task_wait_event(SECOND);
	TEST_EQ(deferred_resume_called, 1, "%d");

	return EC_SUCCESS;
}

test_static int test_storm_not_triggered_for_32bit_overflow(void)
{
	int i;
	timestamp_t time;

	/*
	 * Ensure the MSB is 1 for overflow comparison tests.
	 * But make sure not to move time backwards.
	 */
	time.val = (get_time().val + 0x100000000) | 0xff000000;
	force_time(time);

	/*
	 * 100 events every second for 10 seconds should never trigger
	 * a shutdown call.
	 */
	for (i = 0; i < 10; ++i) {
		num_events = 100;
		deferred_resume_called = 0;
		schedule_deferred_pd_interrupt(PORT0);
		task_wait_event(SECOND);

		TEST_EQ(deferred_resume_called, 0, "%d");
	}

	return EC_SUCCESS;
}

test_static int test_storm_boundary_triggered(void)
{
	/*
	 * Test that interrupt storms spanning across a 1-second window boundary
	 * are detected.
	 * Window 1: ~80% of threshold (< CONFIG_USB_PD_INT_STORM_MAX) -> no
	 * suspend. Window 2: ~70% of threshold (< CONFIG_USB_PD_INT_STORM_MAX
	 * alone, but (80% / 2) + 70% = 110% > MAX with decay) -> triggers
	 * suspend. Without sliding decay: resets to 1 + 70% < MAX -> missed
	 * storm.
	 */
	num_events = (CONFIG_USB_PD_INT_STORM_MAX * 4) / 5 + 1;
	deferred_resume_called = 0;
	schedule_deferred_pd_interrupt(PORT0);
	task_wait_event(1100 * MSEC);
	TEST_EQ(deferred_resume_called, 0, "%d");

	num_events = (CONFIG_USB_PD_INT_STORM_MAX * 7) / 10 + 1;
	schedule_deferred_pd_interrupt(PORT0);
	task_wait_event(1100 * MSEC);
	TEST_EQ(deferred_resume_called, 1, "%d");

	return EC_SUCCESS;
}

static int deferred_executed_count;
static int events_remaining_when_deferred_ran;

static void test_deferred_watchdog_fn(void)
{
	deferred_executed_count++;
	events_remaining_when_deferred_ran = num_events;
}
DECLARE_DEFERRED(test_deferred_watchdog_fn);

test_static int test_alert_burst_yields_to_hooks(void)
{
	/*
	 * Verify that during an extended burst of alerts, the interrupt handler
	 * periodically yields (via crec_usleep) to allow lower-priority tasks
	 * (such as HOOKS task, which executes watchdog reloads and deferred
	 * functions) to run.
	 */
	deferred_executed_count = 0;
	events_remaining_when_deferred_ran = 0;

	/*
	 * 50 consecutive events: with ALERT_BURST_YIELD_THRESHOLD=10,
	 * the task yields every 10 events (after events 10, 20, 30, 40).
	 * Schedule deferred function to fire after 2ms (approx after 20
	 * events).
	 */
	num_events = 51;
	hook_call_deferred(&test_deferred_watchdog_fn_data, 2 * MSEC);

	schedule_deferred_pd_interrupt(PORT0);
	task_wait_event(50 * MSEC);

	TEST_EQ(deferred_executed_count, 1, "%d");
	TEST_GT(events_remaining_when_deferred_ran, 0, "%d");

	return EC_SUCCESS;
}

void before_test(void)
{
	pd_set_suspend(PORT0, 0);
	deferred_resume_called = 0;
	task_wait_event(2 * SECOND);
}

void run_test(int argc, const char **argv)
{
	/* Let tasks settle down */
	task_wait_event(MINUTE);

	RUN_TEST(test_storm_not_triggered);
	RUN_TEST(test_storm_triggered);
	RUN_TEST(test_storm_not_triggered_for_32bit_overflow);
	RUN_TEST(test_storm_boundary_triggered);
	RUN_TEST(test_alert_burst_yields_to_hooks);

	test_print_result();
}
