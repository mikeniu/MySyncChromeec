/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Test API for the ITE IT52XX PDC emulator.
 */

#ifndef __EMUL_ITE_IT52XX_H
#define __EMUL_ITE_IT52XX_H

#include <zephyr/drivers/emul.h>

struct i2c_common_emul_data;

/**
 * @brief Return the common I2C emulator data for fault injection.
 *
 * @param emul IT52XX emulator instance.
 * @return Common I2C emulator data.
 */
struct i2c_common_emul_data *
emul_it52xx_get_i2c_common_data(const struct emul *emul);

#endif /* __EMUL_ITE_IT52XX_H */
