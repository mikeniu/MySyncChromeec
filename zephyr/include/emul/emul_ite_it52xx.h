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

#include "drivers/ucsi_v3.h"

#include <stdbool.h>

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

/**
 * @brief Read the notification mask received during PDC initialization.
 *
 * @param emul IT52XX emulator instance.
 * @param notifications Destination for the notification mask.
 * @return 0 on success or -EINVAL if notifications is NULL.
 */
int emul_it52xx_get_notification_enable(
	const struct emul *emul, union notification_enable_t *notifications);

/**
 * @brief Check whether the expected initialization commands were completed.
 *
 * @param emul IT52XX emulator instance.
 * @return true after PPM_RESET, SET_NOTIFICATION_ENABLE, GET_PDC_INFO, and
 *         their required ACK_CC_CI commands have completed.
 */
bool emul_it52xx_init_complete(const struct emul *emul);

#endif /* __EMUL_ITE_IT52XX_H */
