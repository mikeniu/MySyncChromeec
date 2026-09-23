/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Richtek RT1715 Type-C port controller */

#ifndef PLATFORM_EC_INCLUDE_DRIVER_TCPM_RT1715_PUBLIC_H_
#define PLATFORM_EC_INCLUDE_DRIVER_TCPM_RT1715_PUBLIC_H_

#include "usb_pd_tcpm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2C interface */
#define RT1715_I2C_ADDR_FLAGS 0x4E

#define RT1715_VENDOR_ID 0x29CF

extern const struct tcpm_drv rt1715_tcpm_drv;

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_DRIVER_TCPM_RT1715_PUBLIC_H_ */
