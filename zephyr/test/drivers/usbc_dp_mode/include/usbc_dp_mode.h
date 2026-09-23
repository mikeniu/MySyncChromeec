/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_DRIVERS_USBC_DP_MODE_INCLUDE_USBC_DP_MODE_H_
#define PLATFORM_EC_ZEPHYR_TEST_DRIVERS_USBC_DP_MODE_INCLUDE_USBC_DP_MODE_H_

#define TEST_PORT USBC_PORT_C0
#define PD_SPEC_REVISION 0x3000

#define VDO_MODAL_OPERATION_BIT BIT(26)

/* Remove polarity for any mux checks */
#define USB_MUX_CHECK_MASK ~USB_PD_MUX_POLARITY_INVERTED
#define DPAM_VER_VDO(x) (x << 30)

#include "emul/tcpc/emul_tcpci_partner_snk.h"

extern struct tcpci_cable_data passive_usb3_32;

void add_dp_discovery(struct tcpci_partner_data *partner, int svdm_version);

void add_displayport_mode_responses(struct tcpci_partner_data *partner,
				    int svdm_version);

#endif /* PLATFORM_EC_ZEPHYR_TEST_DRIVERS_USBC_DP_MODE_INCLUDE_USBC_DP_MODE_H_ \
	*/
