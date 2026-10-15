/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>
#include "peripheral_charger.h"

/* Exposed by common/peripheral_charger.c under CONFIG_ZTEST */
const char *test_text_event(enum pchg_event event);
const char *test_text_state(enum pchg_state state);

ZTEST_SUITE(pchg, NULL, NULL, NULL, NULL, NULL);

ZTEST(pchg, test_text_event_bounds)
{
	/* Valid events */
	zassert_not_null(test_text_event(PCHG_EVENT_INITIALIZED));
	
	/* Out of bounds event */
	zassert_equal_ptr("UNDEF", test_text_event(PCHG_EVENT_COUNT));
	zassert_equal_ptr("UNDEF", test_text_event(PCHG_EVENT_COUNT + 10));
}

ZTEST(pchg, test_text_state_bounds)
{
	/* Valid states */
	zassert_not_null(test_text_state(PCHG_STATE_RESET));
	
	/* Out of bounds state */
	zassert_equal_ptr("UNDEF", test_text_state(PCHG_STATE_COUNT));
	zassert_equal_ptr("UNDEF", test_text_state(PCHG_STATE_COUNT + 10));
}
