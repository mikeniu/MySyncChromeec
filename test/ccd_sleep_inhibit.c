/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define TEST_CCD_SLEEP_INHIBIT_MOCKS
#include "ccd_sleep_inhibit.h"
#include "hooks.h"
#include "rdd.h"
#include "test_util.h"
#include "timer.h"

static const int STATE_UPDATE_DELAY_MS = 10;

static int test_rdd_connect_and_usb_suspend_in_g3(void)
{
	mock_ap_on = 0;
	mock_ap_uart_on = 0;
	TEST_ASSERT(gpio_get_level(GPIO_CCD_MODE_L) && !ccd_ext_is_enabled());
	TEST_ASSERT(DEEP_SLEEP_ALLOWED);
	/* 1. Cable attaches: SLEEP_MASK_RDD set before HOOK_SECOND runs. */
	rdd_connect();
	TEST_ASSERT(!gpio_get_level(GPIO_CCD_MODE_L) && !ccd_ext_is_enabled());
	TEST_ASSERT((sleep_mask & SLEEP_MASK_RDD) && !DEEP_SLEEP_ALLOWED);
	/* 2. HOOK_SECOND enables CCD and initializes USB. */
	hook_notify(HOOK_SECOND);
	msleep(STATE_UPDATE_DELAY_MS);
	TEST_ASSERT(ccd_ext_is_enabled());
	TEST_ASSERT(sleep_mask & SLEEP_MASK_USB_DEVICE);
	TEST_ASSERT((sleep_mask & SLEEP_MASK_RDD) && !DEEP_SLEEP_ALLOWED);
	/* 3. USB suspend clears SLEEP_MASK_USB_DEVICE; RDD still inhibits. */
	enable_sleep(SLEEP_MASK_USB_DEVICE);
	TEST_ASSERT(!(sleep_mask & SLEEP_MASK_USB_DEVICE));
	TEST_ASSERT((sleep_mask & SLEEP_MASK_RDD) && !DEEP_SLEEP_ALLOWED);
	/* 4. Cable detaches: SLEEP_MASK_RDD held until CCD teardown. */
	rdd_disconnect();
	TEST_ASSERT(gpio_get_level(GPIO_CCD_MODE_L) && ccd_ext_is_enabled());
	TEST_ASSERT((sleep_mask & SLEEP_MASK_RDD) && !DEEP_SLEEP_ALLOWED);
	/* 5. HOOK_SECOND tears down CCD and re-enables deep sleep. */
	hook_notify(HOOK_SECOND);
	msleep(STATE_UPDATE_DELAY_MS);
	TEST_ASSERT(!ccd_ext_is_enabled());
	TEST_ASSERT(!(sleep_mask & SLEEP_MASK_RDD) && DEEP_SLEEP_ALLOWED);
	/* 6. Transient attach/detach before HOOK_SECOND runs. */
	rdd_connect();
	TEST_ASSERT((sleep_mask & SLEEP_MASK_RDD) && !DEEP_SLEEP_ALLOWED);
	rdd_disconnect();
	TEST_ASSERT(!(sleep_mask & SLEEP_MASK_RDD) && DEEP_SLEEP_ALLOWED);
	return EC_SUCCESS;
}

static int test_ap_uart_interaction_with_rdd_sleep_mask(void)
{
	rdd_connect();
	hook_notify(HOOK_SECOND);
	msleep(STATE_UPDATE_DELAY_MS);
	enable_sleep(SLEEP_MASK_USB_DEVICE);
	mock_ap_on = 1;
	mock_ap_uart_on = 1;
	ccd_update_state();
	msleep(STATE_UPDATE_DELAY_MS);
	TEST_ASSERT(uartn_is_enabled(UART_AP) && (sleep_mask & SLEEP_MASK_RDD));
	/* Case A: AP turns off while cable remains attached. */
	mock_ap_on = 0;
	mock_ap_uart_on = 0;
	ccd_update_state();
	msleep(STATE_UPDATE_DELAY_MS);
	TEST_ASSERT(!uartn_is_enabled(UART_AP) && ccd_ext_is_enabled());
	TEST_ASSERT((sleep_mask & SLEEP_MASK_RDD) && !DEEP_SLEEP_ALLOWED);
	/* Case B: Detach cable while AP UART is still enabled. */
	mock_ap_on = 1;
	mock_ap_uart_on = 1;
	ccd_update_state();
	msleep(STATE_UPDATE_DELAY_MS);
	TEST_ASSERT(uartn_is_enabled(UART_AP));
	rdd_disconnect();
	hook_notify(HOOK_SECOND);
	TEST_ASSERT(!ccd_ext_is_enabled() && uartn_is_enabled(UART_AP));
	TEST_ASSERT((sleep_mask & SLEEP_MASK_RDD) && !DEEP_SLEEP_ALLOWED);
	msleep(STATE_UPDATE_DELAY_MS);
	TEST_ASSERT(!uartn_is_enabled(UART_AP));
	TEST_ASSERT(!(sleep_mask & SLEEP_MASK_RDD) && DEEP_SLEEP_ALLOWED);
	mock_ap_on = 0;
	mock_ap_uart_on = 0;
	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();
	RUN_TEST(test_rdd_connect_and_usb_suspend_in_g3);
	RUN_TEST(test_ap_uart_interaction_with_rdd_sleep_mask);
	test_print_result();
}
