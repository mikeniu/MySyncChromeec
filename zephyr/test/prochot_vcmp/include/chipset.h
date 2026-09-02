/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_PROCHOT_VCMP_INCLUDE_CHIPSET_H_
#define PLATFORM_EC_ZEPHYR_TEST_PROCHOT_VCMP_INCLUDE_CHIPSET_H_

#define CHIPSET_STATE_NOT_ON 0
#define CHIPSET_STATE_ON 1

int chipset_in_state(int state_mask);

#endif /* PLATFORM_EC_ZEPHYR_TEST_PROCHOT_VCMP_INCLUDE_CHIPSET_H_ */
