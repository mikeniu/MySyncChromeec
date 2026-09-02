/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CHARGER_ENUM_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CHARGER_ENUM_H_

/*
 * Theoretically, this should enumerate all the chargers
 * by checking and processing each type, but practically
 * if OCPC is enabled, there are only 2 chargers.
 */
enum chg_id {
	CHARGER_PRIMARY,
#if (CONFIG_USB_PD_PORT_MAX_COUNT > 1)
	CHARGER_SECONDARY,
#endif /* CONFIG_USB_PD_PORT_MAX_COUNT > 1 */
	CHARGER_NUM,
};

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CHARGER_ENUM_H_ */
