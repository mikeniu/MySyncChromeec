<<<<<<< HEAD   (a520918c7c89738378edf64bffe4ab612881c225 mica: Refactor 5V rail control logic)
||||||| BASE   (eeb1a820bd8d6e4770f061372227da0725998655 zephyr: fingerprint: egis630: Move PAL memory allocators fro)
/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MICA_LB_POLICY_H
#define __CROS_EC_MICA_LB_POLICY_H

#define LED_ALT_POLICY_DIAG_RAMDUMP (LED_ALT_POLICY_DIAG_BASE + 0)
#define LED_ALT_POLICY_DIAG_LCD (LED_ALT_POLICY_DIAG_BASE + 1)

#define LIGHTBAR_CMD_SEQ_RAMDUMP 55
#define LIGHTBAR_CMD_SEQ_DIAG_CLEAR 100
#define LIGHTBAR_CMD_SEQ_DIAG_LCD 101

#endif /* __CROS_EC_MICA_LB_POLICY_H */
=======
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
>>>>>>> CHANGE (2f221e4ce8dfaa82e05b18c99219101302964b2a mica: Add a diagnostic lightbar pattern for power failure)
