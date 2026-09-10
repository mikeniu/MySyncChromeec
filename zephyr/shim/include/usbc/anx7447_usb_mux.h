/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_ANX7447_USB_MUX_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_ANX7447_USB_MUX_H_

#include "tcpm/anx7447_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ANX7447_USB_MUX_COMPAT analogix_usbc_mux_anx7447

/* clang-format off */
#define USB_MUX_CONFIG_ANX7447(mux_id)                                         \
	{                                                                      \
		USB_MUX_COMMON_FIELDS(mux_id),                                 \
		.driver = &anx7447_usb_mux_driver,                             \
		.hpd_update = COND_CODE_1(DT_PROP(mux_id, hpd_update_enable),  \
			(&anx7447_tcpc_update_hpd_status), (NULL)),            \
	}
/* clang-format on */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_USBC_ANX7447_USB_MUX_H_ */
