/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ufsc_policies_test.h"

DEFINE_FAKE_VALUE_FUNC(bool, cros_cbi_ufsc_check_match, enum cbi_ufsc_value_id);
