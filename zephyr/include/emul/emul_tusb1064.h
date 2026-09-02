/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_TUSB1064_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_TUSB1064_H_

#include <zephyr/drivers/emul.h>

int tusb1064_emul_peek_reg(const struct emul *emul, int reg);

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_TUSB1064_H_ */
