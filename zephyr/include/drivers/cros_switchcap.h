/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_DRIVERS_CROS_SWITCHCAP_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_DRIVERS_CROS_SWITCHCAP_H_

/**
 * @file
 * @brief ChromiumOS Switched-Capacitor (Switchcap) Driver API.
 */

#include <stdbool.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enable or disable switchcap power rail.
 *
 * @param dev    Pointer to switchcap device structure.
 * @param enable True to enable switchcap rail, false to disable.
 *
 * @retval 0 On success.
 * @return Negative errno code on failure.
 */
int cros_switchcap_set_power(const struct device *dev, bool enable);

/**
 * @brief Check if switchcap power rail is currently enabled.
 *
 * @param dev Pointer to switchcap device structure.
 *
 * @retval true  Switchcap rail is enabled.
 * @retval false Switchcap rail is disabled.
 */
bool cros_switchcap_is_enabled(const struct device *dev);

/**
 * @brief Check if switchcap power rail is in good state.
 *
 * @param dev Pointer to switchcap device structure.
 *
 * @retval true  Power rail is good.
 * @retval false Power rail is not good.
 */
bool cros_switchcap_is_power_good(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_DRIVERS_CROS_SWITCHCAP_H_ */
