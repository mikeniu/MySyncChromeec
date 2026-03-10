// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common.h"
#include "panic.h"
#include "task.h"

/*
 * Override default __stack_chk_fail used by the fstack-protector.
 * Must link with `-Wl,--wrap=__stack_chk_fail`.
 */
void __wrap___stack_chk_fail(void)
{
	panic_set_reason(PANIC_SW_STACK_CANARIES, 0, task_get_current());

	if (IS_ENABLED(CONFIG_PLATFORM_EC_CONSOLE_CMD_CRASH_NESTED))
		command_crash_nested_handler();

	panic_reboot();
	__ASSERT_UNREACHABLE;
}
