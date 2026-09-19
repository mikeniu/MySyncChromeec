/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CROS_SWITCHCAP_H_
#define ZEPHYR_INCLUDE_DRIVERS_CROS_SWITCHCAP_H_

/**
 * @file
 * @brief ChromiumOS Switched-Capacitor (Switchcap) Driver API.
 */

/**
 * @brief ChromiumOS Switched-Capacitor (Switchcap) Driver Interface.
 * @defgroup cros_switchcap_interface Switchcap Driver API
 * @ingroup io_interfaces
 * @{
 */

#include <errno.h>
#include <stdbool.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @cond INTERNAL_HIDDEN */

typedef int (*cros_switchcap_set_power_t)(const struct device *dev,
					  bool enable);
typedef bool (*cros_switchcap_is_power_good_t)(const struct device *dev);
typedef bool (*cros_switchcap_is_enabled_t)(const struct device *dev);

__subsystem struct cros_switchcap_driver_api {
	cros_switchcap_set_power_t set_power;
	cros_switchcap_is_power_good_t is_power_good;
	cros_switchcap_is_enabled_t is_enabled;
};

/** @endcond */

/**
 * @brief Enable or disable switchcap power rail.
 *
 * @param dev    Pointer to switchcap device structure (may be NULL).
 * @param enable True to enable switchcap rail, false to disable.
 *
 * @retval 0       On success (or if @p dev is NULL).
 * @retval -ENOSYS If the driver does not implement the set_power callback.
 * @return Negative errno code on failure.
 */
static inline int cros_switchcap_set_power(const struct device *dev,
					   bool enable)
{
	if (!dev)
		return 0;

	const struct cros_switchcap_driver_api *api =
		DEVICE_API_GET(cros_switchcap, dev);

	if (!api || !api->set_power)
		return -ENOSYS;
	return api->set_power(dev, enable);
}

/**
 * @brief Check if switchcap power rail is in good state.
 *
 * @param dev Pointer to switchcap device structure (may be NULL).
 *
 * @retval true  Power rail is good (or if @p dev is NULL).
 * @retval false Power rail is not good.
 */
static inline bool cros_switchcap_is_power_good(const struct device *dev)
{
	if (!dev)
		return true;

	const struct cros_switchcap_driver_api *api =
		DEVICE_API_GET(cros_switchcap, dev);

	if (!api || !api->is_power_good)
		return true;
	return api->is_power_good(dev);
}

/**
 * @brief Check if switchcap power rail is currently enabled.
 *
 * @param dev Pointer to switchcap device structure (may be NULL).
 *
 * @retval true  Switchcap rail is enabled.
 * @retval false Switchcap rail is disabled (or if @p dev is NULL).
 */
static inline bool cros_switchcap_is_enabled(const struct device *dev)
{
	if (!dev)
		return false;

	const struct cros_switchcap_driver_api *api =
		DEVICE_API_GET(cros_switchcap, dev);

	if (!api || !api->is_enabled)
		return false;
	return api->is_enabled(dev);
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_CROS_SWITCHCAP_H_ */
