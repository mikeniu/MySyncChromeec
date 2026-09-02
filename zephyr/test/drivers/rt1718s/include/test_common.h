/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_DRIVERS_RT1718S_INCLUDE_TEST_COMMON_H_
#define PLATFORM_EC_ZEPHYR_TEST_DRIVERS_RT1718S_INCLUDE_TEST_COMMON_H_

#include <zephyr/drivers/emul.h>

extern const int tcpm_rt1718s_port;
extern const struct emul *rt1718s_emul;

void rt1718s_clear_set_reg_history(void *f);

void compare_reg_val_with_mask(const struct emul *emul, int reg,
			       uint16_t expected, uint16_t mask);

#endif /* PLATFORM_EC_ZEPHYR_TEST_DRIVERS_RT1718S_INCLUDE_TEST_COMMON_H_ */
