/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kinetic KTU1125 USB-C Power Path Controller */

#ifndef PLATFORM_EC_INCLUDE_DRIVER_PPC_KTU1125_PUBLIC_H_
#define PLATFORM_EC_INCLUDE_DRIVER_PPC_KTU1125_PUBLIC_H_

#define KTU1125_ADDR0_FLAGS 0x78
#define KTU1125_ADDR1_FLAGS 0x79
#define KTU1125_ADDR2_FLAGS 0x7A
#define KTU1125_ADDR3_FLAGS 0x7B

extern const struct ppc_drv ktu1125_drv;

/**
 * Interrupt Handler for the KTU1125.
 *
 * @param port: The Type-C port which triggered the interrupt.
 */
void ktu1125_interrupt(int port);

#endif /* PLATFORM_EC_INCLUDE_DRIVER_PPC_KTU1125_PUBLIC_H_ */
