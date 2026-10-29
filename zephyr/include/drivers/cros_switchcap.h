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

/** @cond INTERNAL_HIDDEN */

typedef int (*cros_switchcap_set_power_t)(const struct device *dev,
					  bool enable);
typedef bool (*cros_switchcap_is_enabled_t)(const struct device *dev);
typedef bool (*cros_switchcap_is_power_good_t)(const struct device *dev);

__subsystem struct cros_switchcap_driver_api {
	cros_switchcap_set_power_t set_power;
	cros_switchcap_is_enabled_t is_enabled;
	cros_switchcap_is_power_good_t is_power_good;
};

/** @endcond */

/**
 * @brief Enable or disable switchcap power rail.
 *
 * @param dev    Pointer to switchcap device structure.
 * @param enable True to enable switchcap rail, false to disable.
 *
 * @retval 0 On success.
 * @return Negative errno code on failure.
 */
static inline int cros_switchcap_set_power(const struct device *dev,
					   bool enable)
{
	return DEVICE_API_GET(cros_switchcap, dev)->set_power(dev, enable);
}

/**
 * @brief Check if switchcap power rail is currently enabled.
 *
 * @param dev Pointer to switchcap device structure.
 *
 * @retval true  Switchcap rail is enabled.
 * @retval false Switchcap rail is disabled.
 */
static inline bool cros_switchcap_is_enabled(const struct device *dev)
{
	return DEVICE_API_GET(cros_switchcap, dev)->is_enabled(dev);
}

/**
 * @brief Check if switchcap power rail is in good state.
 *
 * @param dev Pointer to switchcap device structure.
 *
 * @retval true  Power rail is good.
 * @retval false Power rail is not good.
 */
static inline bool cros_switchcap_is_power_good(const struct device *dev)
{
	return DEVICE_API_GET(cros_switchcap, dev)->is_power_good(dev);
}

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_DRIVERS_CROS_SWITCHCAP_H_ */
