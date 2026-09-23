/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if !defined(__CROS_EC_CONFIG_CHIP_H) || \
	defined(PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_BATTERY_ENUM_H_)
#error "This file must only be included from config_chip.h and it should be" \
	"included in all zephyr builds automatically"
#endif

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_BATTERY_ENUM_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_BATTERY_ENUM_H_

#define BATTERY_ENUM(val) DT_CAT(BATTERY_, val)
#define BATTERY_TYPE(id) BATTERY_ENUM(DT_STRING_UPPER_TOKEN(id, enum_name))
#define BATTERY_TYPE_WITH_COMMA(id) BATTERY_TYPE(id),

/* This produces a list of BATTERY_<ENUM_NAME> identifiers */
/* clang-format off */
enum battery_type {
#if DT_HAS_COMPAT_STATUS_OKAY(battery_smart)
	DT_FOREACH_STATUS_OKAY(battery_smart, BATTERY_TYPE_WITH_COMMA)
#endif
	BATTERY_TYPE_COUNT,
};
/* clang-format on */

#undef BATTERY_TYPE_WITH_COMMA

#if DT_NODE_EXISTS(DT_NODELABEL(default_battery_3s))
extern const enum battery_type DEFAULT_BATTERY_TYPE_3S;
#endif
#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_BATTERY_ENUM_H_ */
