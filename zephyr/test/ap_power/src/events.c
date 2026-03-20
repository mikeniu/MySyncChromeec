/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for AP power events
 */

#include "ap_power/ap_power.h"
#include "ap_power/ap_power_events.h"
#include "hooks.h"
#include "test_state.h"

#include <zephyr/device.h>
#include <zephyr/drivers/espi.h>
#include <zephyr/drivers/espi_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

/* Shared state for tests */
static int ev_count;
static enum ap_power_events ev_last_event;

static void ev_handler(struct ap_power_ev_callback *cb,
		       struct ap_power_ev_data data)
{
	ev_count++;
	ev_last_event = data.event;
}
AP_POWER_EVENT_CALLBACK_DEFINE(ev_handler, AP_POWER_RESET, AP_POWER_SUSPEND,
			       AP_POWER_RESUME, AP_POWER_STARTUP,
			       AP_POWER_SHUTDOWN);

/**
 * @brief TestPurpose: Check registration
 *
 * @details
 * Validate that the callback fires for its registered event and not for others.
 *
 * Expected Results
 *  - Callback fires for registered events only.
 */
ZTEST(events, test_registration)
{
	ev_count = 0;
	ev_last_event = 0;
	ap_power_ev_send_callbacks(AP_POWER_RESET);
	zassert_equal(1, ev_count, "Callback not called");
	zassert_equal(AP_POWER_RESET, ev_last_event, "Wrong event");
	/* Verify unregistered event does not trigger callback */
	ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
	zassert_equal(1, ev_count, "Callback called for unregistered event");
}

/**
 * @brief TestPurpose: Verify reset callback from ESPI
 *
 * @details
 * Validate that the reset callback is sent with ESPI PLTRST#
 *
 * Expected Results
 *  - The AP_POWER_RESET event is sent
 */
ZTEST(events, test_pltrst)
{
	const struct device *espi =
		DEVICE_DT_GET_ANY(zephyr_espi_emul_controller);

	zassert_not_null(espi, "Cannot get ESPI device");
	ev_count = 0;
	ev_last_event = 0;
	emul_espi_host_send_vw(espi, ESPI_VWIRE_SIGNAL_PLTRST, 0);
	/*
	 * Since the event is being sent via a deferred function,
	 * wait for the deferral time.
	 */
	k_usleep(2 * 1000);
	zassert_equal(1, ev_count, "Callback not called");
	zassert_equal(AP_POWER_RESET, ev_last_event, "Wrong event");
}

/**
 * @brief TestPurpose: Check event mask
 *
 * @details
 * Validate that the callback fires only for events in its static mask.
 *
 * Expected Results
 *  - Callback fires for registered events; unregistered events are ignored.
 */
ZTEST(events, test_event_mask)
{
	ev_count = 0;
	ap_power_ev_send_callbacks(AP_POWER_RESET);
	zassert_equal(1, ev_count, "Callback not called for RESET");
	ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
	zassert_equal(2, ev_count, "Callback not called for SUSPEND");
	/* Verify event not in mask does not fire */
	ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
	zassert_equal(2, ev_count, "Callback called for unregistered event");
}

static int count_hook_shutdown, count_hook_startup;

static void hook_shutdown(void)
{
	count_hook_shutdown++;
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, hook_shutdown, HOOK_PRIO_DEFAULT);

static void hook_startup(void)
{
	count_hook_startup++;
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, hook_startup, HOOK_PRIO_DEFAULT);

/**
 * @brief TestPurpose: Verify correct interconnection with hook framework.
 *
 * @details
 * Validate that events get passed back to the hook subsystem.
 *
 * Expected Results
 *  - Events originating from the AP power event API get delivered via hooks.
 */
ZTEST(events, test_hooks)
{
	count_hook_startup = count_hook_shutdown = 0;
	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	zassert_equal(0, count_hook_shutdown, "shutdown hook called");
	zassert_equal(1, count_hook_startup, "startup hook not called");
	zassert_equal(0, count_hook_shutdown,
		      "reset event, shutdown hook called");
	zassert_equal(1, count_hook_startup,
		      "reset event, startup hook called");
	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
	zassert_equal(1, count_hook_shutdown, "shutdown hook not called");
	zassert_equal(1, count_hook_startup, "startup hook called");
}

/**
 * @brief Test Suite: Verifies AP power notification functionality.
 */
ZTEST_SUITE(events, ap_power_predicate_post_main, NULL, NULL, NULL, NULL);
