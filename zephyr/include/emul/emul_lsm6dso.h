/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_LSM6DSO_H
#define __EMUL_LSM6DSO_H

#include <zephyr/drivers/emul.h>

/**
 * @brief Set the register that will fail on write
 *
 * @param target The emulator target
 * @param reg The register to fail on write, or -1 to disable
 */
void lsm6dso_emul_set_write_fail_reg(const struct emul *target, int reg);

#endif /* __EMUL_LSM6DSO_H */
