/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LIS2DW12 gsensor module for Chrome EC */

#ifndef PLATFORM_EC_INCLUDE_DRIVER_ACCEL_LIS2DH_PUBLIC_H_
#define PLATFORM_EC_INCLUDE_DRIVER_ACCEL_LIS2DH_PUBLIC_H_

#include "config.h"
#include "gpio_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

void lis2dh_interrupt(enum gpio_signal signal);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_DRIVER_ACCEL_LIS2DH_PUBLIC_H_ */
