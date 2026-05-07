/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"
#include "wp_custom.h"

bool dev_unlocked;

void enable_write_protect_custom(void)
{
	dev_unlocked = true;
}

__weak bool write_protect_is_asserted_custom(void)
{
	if (dev_unlocked) {
		return false;
	}

	return true;
}
