/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TI TUSB422 Type-C port controller */

#ifndef PLATFORM_EC_INCLUDE_DRIVER_TCPM_TUSB422_PUBLIC_H_
#define PLATFORM_EC_INCLUDE_DRIVER_TCPM_TUSB422_PUBLIC_H_

/* I2C interface */
#define TUSB422_I2C_ADDR_FLAGS 0x20

extern const struct tcpm_drv tusb422_tcpm_drv;

#endif /* PLATFORM_EC_INCLUDE_DRIVER_TCPM_TUSB422_PUBLIC_H_ */
