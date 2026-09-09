/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_I2C_I2C_DTS_PASSTHRU_H_
#define ZEPHYR_DRIVERS_I2C_I2C_DTS_PASSTHRU_H_

#include <zephyr/device.h>

#if defined(CONFIG_ZTEST)
const struct device *i2c_portmap(int remote_port);
#endif

#endif /* ZEPHYR_DRIVERS_I2C_I2C_DTS_PASSTHRU_H_ */
