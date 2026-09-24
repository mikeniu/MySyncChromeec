/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * Set display backlight brightness
 *
 * @param percent Brightness in percentage
 * @return EC_SUCCESS or EC_ERROR_*
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_DRIVERS_CROS_DISPLIGHT_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_DRIVERS_CROS_DISPLIGHT_H_

int displight_set(int percent);

/**
 * Get display backlight brightness
 *
 * @return Brightness in percentage
 */
int displight_get(void);

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_DRIVERS_CROS_DISPLIGHT_H_ */
