/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_POWER_POWER_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_POWER_POWER_H_

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_CROS_EC_POWER_SIGNAL_LIST

#define POWER_SIGNAL_LIST_NODE DT_NODELABEL(power_signal_list)

#define GEN_POWER_SIGNAL_ENUM_ENTRY(cid) \
	DT_STRING_UPPER_TOKEN(cid, power_enum_name)

enum power_signal {
	DT_FOREACH_CHILD_SEP(POWER_SIGNAL_LIST_NODE,
			     GEN_POWER_SIGNAL_ENUM_ENTRY, (, )),
	POWER_SIGNAL_COUNT
};

#endif /* CONFIG_CROS_EC_POWER_SIGNAL_LIST */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_POWER_POWER_H_ */
