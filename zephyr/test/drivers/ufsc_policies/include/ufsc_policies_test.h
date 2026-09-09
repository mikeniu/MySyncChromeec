/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_UFSC_POLICIES_TEST_H_
#define ZEPHYR_TEST_DRIVERS_UFSC_POLICIES_TEST_H_

#include "cros_cbi.h"

#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(bool, cros_cbi_ufsc_check_match,
			enum cbi_ufsc_value_id);

#endif /* ZEPHYR_TEST_DRIVERS_UFSC_POLICIES_TEST_H_ */
