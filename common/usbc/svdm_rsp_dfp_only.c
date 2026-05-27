/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * SVDM identity support for DFP-only devices.
 *
 * This file is only supported for Zephyr builds, enabled by
 * CONFIG_SVDM_RSP_DFP_ONLY. No equivalent config exists for EC-OS.
 */

#include "usb_pd.h"

static const uint32_t vdo_idh_no_ufp = VDO_IDH(1, /* Data caps as USB host */
					       0, /* Not a USB device */
					       IDH_PTYPE_UNDEF, 0, /* No UFP
								    * modal
								    * operation
								    */
					       USB_VID_GOOGLE);

static const uint32_t vdo_idh_tbt = VDO_IDH(1, /* Data caps as USB host */
					    0, /* Not a USB device */
					    IDH_PTYPE_PERIPH, 1, /* UFP modal
								  * operation
								  */
					    USB_VID_GOOGLE);

static const uint32_t vdo_idh_rev30_no_ufp =
	VDO_IDH_REV30(1, /* Data caps as USB host */
		      0, /* Not a USB device */
		      IDH_PTYPE_UNDEF, 0, /* No UFP modal operation */
		      IDH_PTYPE_DFP_HOST, USB_TYPEC_RECEPTACLE, USB_VID_GOOGLE);

static const uint32_t vdo_idh_rev30_tbt =
	VDO_IDH_REV30(1, /* Data caps as USB host */
		      0, /* Not a USB device */
		      IDH_PTYPE_PERIPH, 1, /* UFP modal operation */
		      IDH_PTYPE_DFP_HOST, USB_TYPEC_RECEPTACLE, USB_VID_GOOGLE);

static const uint32_t vdo_product =
	VDO_PRODUCT(CONFIG_USB_PID, CONFIG_USB_BCD_DEV);

static const uint32_t vdo_ufp1 = VDO_UFP1(
	(VDO_UFP1_CAPABILITY_USB20 | VDO_UFP1_CAPABILITY_USB32 |
	 VDO_UFP1_CAPABILITY_USB4),
	USB_TYPEC_RECEPTACLE, VDO_UFP1_ALT_MODE_TBT3, USB_R30_SS_U40_GEN3);

/* Track whether we've been enabled to ACK TBT EnterModes requests */
static bool tbt_ufp_ack_allowed[CONFIG_USB_PD_PORT_MAX_COUNT];

static int svdm_identity(int port, uint32_t *payload)
{
	/* Determine host capability based on configuration */
	uint32_t host_cap = VDO_DFP_HOST_CAPABILITY_USB20 |
			    VDO_DFP_HOST_CAPABILITY_USB32;

#ifdef CONFIG_USB_PD_USB4
	host_cap |= VDO_DFP_HOST_CAPABILITY_USB4;
#endif

	uint32_t vdo_dfp = VDO_DFP(host_cap, USB_TYPEC_RECEPTACLE, port);

	/* The SVID in the Discover Identity Command request Shall be set to the
	 * PD SID */
	if (PD_VDO_VID(payload[VDO_INDEX_HDR]) == USB_SID_PD) {
		payload[VDO_I(CSTAT)] = VDO_CSTAT(CONFIG_USB_PD_XID);
		payload[VDO_I(PRODUCT)] = vdo_product;

		if (pd_get_rev(port, TCPCI_MSG_SOP) == PD_REV30) {
			payload[VDO_I(PTYPE_UFP2_VDO)] = 0;
			payload[VDO_I(PTYPE_DFP_VDO)] = vdo_dfp;
			/* PD Revision 3.0 */
			if (tbt_ufp_ack_allowed[port]) {
				payload[VDO_I(IDH)] = vdo_idh_rev30_tbt;
				payload[VDO_I(PTYPE_UFP1_VDO)] = vdo_ufp1;
				return VDO_I(PTYPE_DFP_VDO) + 1;
			} else {
				payload[VDO_I(IDH)] = vdo_idh_rev30_no_ufp;
				payload[VDO_I(PTYPE_UFP1_VDO)] = 0;
				return VDO_I(PTYPE_UFP1_VDO) + 1;
			}
		}
		/* PD Revision 2.0 */

		if (tbt_ufp_ack_allowed[port]) {
			payload[VDO_I(IDH)] = vdo_idh_tbt;
		} else {
			payload[VDO_I(IDH)] = vdo_idh_no_ufp;
		}

		return VDO_I(PRODUCT) + 1;
	} else {
		return 0; /* NAK */
	}
}

__override const struct svdm_response svdm_rsp = {
	.identity = svdm_identity,
	/*
	 * Discover Identity support is required for devices with more than one
	 * DFP, but other SVDM commands are optional. We don't support operating
	 * as Responder in any mode, so leave them unimplemented. See 6.13.5,
	 * Applicability of Structured VDM Commands.
	 */
};
