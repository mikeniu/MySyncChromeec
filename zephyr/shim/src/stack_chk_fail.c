// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "builtin/assert.h"

#include <zephyr/kernel.h>

/*
 * Override default __stack_chk_fail used by the fstack-protector.
 * Must link with `-Wl,--wrap=__stack_chk_fail`.
 */
void __wrap___stack_chk_fail(void)
{
	k_panic();
	__ASSERT_UNREACHABLE;
}
