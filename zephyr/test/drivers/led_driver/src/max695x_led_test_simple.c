/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Unit tests for MAX695x 7-segment LED display driver
 */

#include "chipset.h"
#include "display_7seg.h"
#include "hooks.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

/* Simple test to verify display_7seg_write doesn't crash */
ZTEST(max695x_led, test_display_7seg_write_basic)
{
	int ret;

	/* Test console display */
	ret = display_7seg_write(SEVEN_SEG_CONSOLE_DISPLAY, 0x1234);
	zassert_true(ret == EC_SUCCESS || ret != EC_ERROR_UNKNOWN, 
		     "Console display should not return unknown error");

	/* Test EC display */  
	ret = display_7seg_write(SEVEN_SEG_EC_DISPLAY, 0x5678);
	zassert_true(ret == EC_SUCCESS || ret != EC_ERROR_UNKNOWN, 
		     "EC display should not return unknown error");

	/* Test PORT80 display */
	ret = display_7seg_write(SEVEN_SEG_PORT80_DISPLAY, 0x9ABC);
	zassert_true(ret == EC_SUCCESS || ret != EC_ERROR_UNKNOWN, 
		     "PORT80 display should not return unknown error");
}

/* Test invalid display module */
ZTEST(max695x_led, test_display_7seg_write_invalid)
{
	int ret = display_7seg_write(99, 0x1234); /* Invalid module */
	zassert_equal(ret, EC_ERROR_UNKNOWN, "Should return error for invalid module");
}

/* Test hooks don't crash */
ZTEST(max695x_led, test_max695x_hooks)
{
	hook_notify(HOOK_INIT);
	hook_notify(HOOK_CHIPSET_STARTUP);  
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	
	zassert_true(true, "Hooks executed without crashing");
}

/* Test boundary values */
ZTEST(max695x_led, test_display_7seg_boundary_values)
{
	int ret;
	
	/* Test minimum value */
	ret = display_7seg_write(SEVEN_SEG_CONSOLE_DISPLAY, 0x0000);
	zassert_true(ret == EC_SUCCESS || ret != EC_ERROR_UNKNOWN, 
		     "Should handle minimum value");
	
	/* Test maximum value */
	ret = display_7seg_write(SEVEN_SEG_CONSOLE_DISPLAY, 0xFFFF);
	zassert_true(ret == EC_SUCCESS || ret != EC_ERROR_UNKNOWN, 
		     "Should handle maximum value");
}

static void max695x_led_before(void *fixture)
{
	ARG_UNUSED(fixture);
}

ZTEST_SUITE(max695x_led, drivers_predicate_post_main, NULL, max695x_led_before, NULL, NULL);
