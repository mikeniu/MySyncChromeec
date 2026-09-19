/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "mock_chipset.h"
#include "system.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

atomic_t sleep_mask;
bool mock_can_boot_ap = true;
uint32_t mock_reset_flags;
int mock_hook_notify_count[HOOK_TYPE_COUNT];

int system_can_boot_ap(void)
{
	return mock_can_boot_ap;
}

uint32_t system_get_reset_flags(void)
{
	return mock_reset_flags;
}

void hook_notify(enum hook_type type)
{
	zassert_true(type >= 0 && type < HOOK_TYPE_COUNT);
	mock_hook_notify_count[type]++;
}

/* Additional functions for test verification */

void mock_chipset_reset_state(void)
{
	mock_can_boot_ap = true;
	mock_reset_flags = 0;
	memset(mock_hook_notify_count, 0, sizeof(mock_hook_notify_count));
}

int mock_get_hook_notify_count(enum hook_type type)
{
	zassert_true(type >= 0 && type < HOOK_TYPE_COUNT);
	return mock_hook_notify_count[type];
}

void mock_clear_hook_notify_counts(void)
{
	memset(mock_hook_notify_count, 0, sizeof(mock_hook_notify_count));
}
