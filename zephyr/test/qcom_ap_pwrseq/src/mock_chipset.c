/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "mock_chipset.h"
#include "system.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DEFINE_FAKE_VALUE_FUNC(int, system_can_boot_ap);

FAKE_VOID_FUNC(test_hook_pre_init);
FAKE_VOID_FUNC(test_hook_startup);
FAKE_VOID_FUNC(test_hook_resume);
FAKE_VOID_FUNC(test_hook_suspend);
FAKE_VOID_FUNC(test_hook_shutdown);
FAKE_VOID_FUNC(test_hook_shutdown_complete);
FAKE_VOID_FUNC(test_hook_reset);

DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, test_hook_pre_init, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, test_hook_startup, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, test_hook_resume, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, test_hook_suspend, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, test_hook_shutdown, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN_COMPLETE, test_hook_shutdown_complete,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESET, test_hook_reset, HOOK_PRIO_DEFAULT);

/* Additional functions for test verification */

void mock_clear_hook_notify_counts(void)
{
	RESET_FAKE(test_hook_pre_init);
	RESET_FAKE(test_hook_startup);
	RESET_FAKE(test_hook_resume);
	RESET_FAKE(test_hook_suspend);
	RESET_FAKE(test_hook_shutdown);
	RESET_FAKE(test_hook_shutdown_complete);
	RESET_FAKE(test_hook_reset);
}

void mock_chipset_reset_state(void)
{
	RESET_FAKE(system_can_boot_ap);
	system_can_boot_ap_fake.return_val = 1;
	system_common_reset_state();
	mock_clear_hook_notify_counts();
}

int mock_get_hook_notify_count(enum hook_type type)
{
	switch (type) {
	case HOOK_CHIPSET_PRE_INIT:
		return test_hook_pre_init_fake.call_count;
	case HOOK_CHIPSET_STARTUP:
		return test_hook_startup_fake.call_count;
	case HOOK_CHIPSET_RESUME:
		return test_hook_resume_fake.call_count;
	case HOOK_CHIPSET_SUSPEND:
		return test_hook_suspend_fake.call_count;
	case HOOK_CHIPSET_SHUTDOWN:
		return test_hook_shutdown_fake.call_count;
	case HOOK_CHIPSET_SHUTDOWN_COMPLETE:
		return test_hook_shutdown_complete_fake.call_count;
	case HOOK_CHIPSET_RESET:
		return test_hook_reset_fake.call_count;
	default:
		zassert_unreachable("Unsupported hook type %d", type);
		return 0;
	}
}
