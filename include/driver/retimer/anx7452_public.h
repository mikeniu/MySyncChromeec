/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ANX7452: Active redriver
 *
 * Public functions, definitions, and structures.
 */

#ifndef PLATFORM_EC_INCLUDE_DRIVER_RETIMER_ANX7452_PUBLIC_H_
#define PLATFORM_EC_INCLUDE_DRIVER_RETIMER_ANX7452_PUBLIC_H_

#include "usb_mux.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const struct usb_mux_driver anx7452_usb_retimer_driver;

/* Retimer driver hardware specific controls */
struct anx7452_control {
	/* USB enable */
	const enum gpio_signal usb_enable_gpio;
	/* DP enable */
	const enum gpio_signal dp_enable_gpio;
};
extern const struct anx7452_control anx7452_controls[];

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_DRIVER_RETIMER_ANX7452_PUBLIC_H_ */
