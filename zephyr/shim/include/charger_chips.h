/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CHARGER_CHIPS_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CHARGER_CHIPS_H_

#include "charger.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const struct charger_config_t chg_chips_alt[];

#define ALT_CHG_CHIP_CHK(usbc_id, usb_port_num)                              \
	COND_CODE_1(DT_REG_HAS_IDX(usbc_id, usb_port_num),                   \
		    (COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, chg_alt), (|| 1), \
				 (|| 0))),                                   \
		    (|| 0))

#define CHG_ENABLE_ALTERNATE(usb_port_num)                                     \
	do {                                                                   \
		BUILD_ASSERT(                                                  \
			(0 DT_FOREACH_STATUS_OKAY_VARGS(named_usbc_port,       \
							ALT_CHG_CHIP_CHK,      \
							usb_port_num)),        \
			"Selected USB node does not exist or does not specify" \
			"a charger alternate chip");                           \
		memcpy(&chg_chips[usb_port_num], &chg_chips_alt[usb_port_num], \
		       sizeof(struct charger_config_t));                       \
	} while (0)

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_CHARGER_CHIPS_H_ */
