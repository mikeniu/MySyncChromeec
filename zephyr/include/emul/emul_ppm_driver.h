/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_PPM_DRIVER_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_PPM_DRIVER_H_

#include "usbc/ppm.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>

void emul_ppm_driver_reset(void);

DECLARE_FAKE_VALUE_FUNC(int, ppm_driver_mock_init_ppm, const struct device *);
DECLARE_FAKE_VALUE_FUNC(struct ucsi_ppm_device *, ppm_driver_mock_get_ppm_dev,
			const struct device *);
DECLARE_FAKE_VALUE_FUNC(int, ppm_driver_mock_execute_cmd_sync,
			const struct device *, struct ucsi_control_t *,
			uint8_t *);
DECLARE_FAKE_VALUE_FUNC(int, ppm_driver_mock_get_active_port_count,
			const struct device *);

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_EMUL_EMUL_PPM_DRIVER_H_ */
