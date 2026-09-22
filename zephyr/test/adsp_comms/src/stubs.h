/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __TEST_ADSP_COMMS_STUBS_H
#define __TEST_ADSP_COMMS_STUBS_H

#include "battery.h"
#include "battery_smart.h"
#include "chipset.h"
#include "extpower.h"
#include "i2c.h"

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, chipset_in_state, int);
DECLARE_FAKE_VALUE_FUNC(int, extpower_is_present);
DECLARE_FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
DECLARE_FAKE_VALUE_FUNC(int, sb_read, int, int *);

void stubs_reset(void);

#endif /* __TEST_ADSP_COMMS_STUBS_H */
