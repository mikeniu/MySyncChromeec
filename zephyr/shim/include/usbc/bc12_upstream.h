/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_BC12_UPSTREAM_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_BC12_UPSTREAM_H_

/* Compatible properties for all upstream drivers */
#define PI3USB9201_UPSTREAM_COMPAT diodes_pi3usb9201

#define BC12_CHIP_UPSTREAM(id)             \
	{                                  \
		.drv = &bc12_upstream_drv, \
	},

extern const struct bc12_drv bc12_upstream_drv;

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_BC12_UPSTREAM_H_ */
