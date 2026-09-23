/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_TOUCHPAD_ELAN_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_TOUCHPAD_ELAN_H_

#include <zephyr/drivers/emul.h>

void touchpad_elan_emul_set_raw_report(const struct emul *emul,
				       const uint8_t *report);

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_TOUCHPAD_ELAN_H_ */
