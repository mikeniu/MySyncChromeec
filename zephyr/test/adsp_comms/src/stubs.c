/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "stubs.h"

#include <string.h>

DEFINE_FAKE_VALUE_FUNC(int, chipset_in_state, int);
DEFINE_FAKE_VALUE_FUNC(int, extpower_is_present);
DEFINE_FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
DEFINE_FAKE_VALUE_FUNC(int, sb_read, int, int *);

void stubs_reset(void)
{
	RESET_FAKE(chipset_in_state);
	RESET_FAKE(extpower_is_present);
	RESET_FAKE(battery_is_present);
	RESET_FAKE(sb_read);
	battery_set_fake_soc(-1);
}
