/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_RT9490_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_RT9490_H_

#include <zephyr/drivers/emul.h>

void rt9490_emul_reset_regs(const struct emul *emul);

int rt9490_emul_peek_reg(const struct emul *emul, int reg);

int rt9490_emul_write_reg(const struct emul *emul, int reg, int val);

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_RT9490_H_ */
