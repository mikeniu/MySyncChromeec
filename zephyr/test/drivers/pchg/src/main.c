/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "peripheral_charger.h"

#include <string.h>

#include <zephyr/ztest.h>

/* Exposed by common/peripheral_charger.c under CONFIG_ZTEST */
const char *test_text_event(enum pchg_event event);
const char *test_text_state(enum pchg_state state);

ZTEST_SUITE(pchg, NULL, NULL, NULL, NULL, NULL);

ZTEST(pchg, test_text_event_bounds)
{
	/* Valid events */
	zassert_not_null(test_text_event(PCHG_EVENT_INITIALIZED));

	/* Out of bounds event */
	zassert_equal(0, strcmp("UNDEF", test_text_event(PCHG_EVENT_COUNT)));
	zassert_equal(0,
		      strcmp("UNDEF", test_text_event(PCHG_EVENT_COUNT + 10)));
}

ZTEST(pchg, test_text_state_bounds)
{
	/* Valid states */
	zassert_not_null(test_text_state(PCHG_STATE_RESET));

	/* Out of bounds state */
	zassert_equal(0, strcmp("UNDEF", test_text_state(PCHG_STATE_COUNT)));
	zassert_equal(0,
		      strcmp("UNDEF", test_text_state(PCHG_STATE_COUNT + 10)));
}
