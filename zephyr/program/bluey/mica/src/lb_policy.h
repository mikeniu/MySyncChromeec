/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MICA_LB_POLICY_H
#define __CROS_EC_MICA_LB_POLICY_H

#include "common/lightbar_policy_alt.h"

#define LED_ALT_POLICY_DIAG_RAMDUMP (LED_ALT_POLICY_DIAG_BASE + 0)
#define LED_ALT_POLICY_DIAG_LCD (LED_ALT_POLICY_DIAG_BASE + 1)
#define LED_ALT_POLICY_DIAG_PWR (LED_ALT_POLICY_DIAG_BASE + 2)

#define LIGHTBAR_CMD_SEQ_RAMDUMP 55
#define LIGHTBAR_CMD_SEQ_DIAG_CLEAR 100
#define LIGHTBAR_CMD_SEQ_DIAG_LCD 101

#endif /* __CROS_EC_MICA_LB_POLICY_H */
