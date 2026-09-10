/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ITE PD INTC control module */

#ifndef PLATFORM_EC_DRIVER_TCPM_ITE_PD_INTC_H_
#define PLATFORM_EC_DRIVER_TCPM_ITE_PD_INTC_H_

/**
 * ITE embedded PD interrupt routine
 *
 * NOTE: Enable ITE embedded PD that it requires CONFIG_USB_PD_TCPM_ITE_ON_CHIP
 *
 * @param port Type-C port number
 *
 * @return none
 */
void chip_pd_irq(enum usbpd_port port);

#endif /* PLATFORM_EC_DRIVER_TCPM_ITE_PD_INTC_H_ */
