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

static int svdm_identity(int port, uint32_t *payload)
{
	bool tbt_mode = IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE);
	bool usb32_drd = IS_ENABLED(CONFIG_USB_PD_USB32_DRD);
	bool usb4_drd = IS_ENABLED(CONFIG_USB_PD_USB4_DRD);

	bool has_ufp = tbt_mode || usb32_drd || usb4_drd;

	uint32_t ufp_type = has_ufp ? IDH_PTYPE_PERIPH : IDH_PTYPE_UNDEF;

	/* The SVID in the Discover Identity Command request Shall be set to the
	 * PD SID */
	if (PD_VDO_VID(payload[VDO_INDEX_HDR]) != USB_SID_PD) {
		return 0;
	}

	payload[VDO_I(CSTAT)] = VDO_CSTAT(CONFIG_USB_PD_XID);
	payload[VDO_I(PRODUCT)] =
		VDO_PRODUCT(CONFIG_USB_PID, CONFIG_USB_BCD_DEV);

	if (pd_get_rev(port, TCPCI_MSG_SOP) < PD_REV30) {
		payload[VDO_I(IDH)] = VDO_IDH(1, /* USB host */
					      has_ufp, /* USB device capable */
					      ufp_type, has_ufp, /* UFP modal
								     support */
					      CONFIG_USB_VID);

		return VDO_I(PRODUCT) + 1;
	}

	/* Determine host capability based on configuration */
	uint32_t host_cap = VDO_DFP_HOST_CAPABILITY_USB20 |
			    VDO_DFP_HOST_CAPABILITY_USB32;

#ifdef CONFIG_USB_PD_USB4
	host_cap |= VDO_DFP_HOST_CAPABILITY_USB4;
#endif

	/* PD 3.0 */
	payload[VDO_I(IDH)] =
		VDO_IDH_REV30(1, /* USB host */
			      has_ufp, /* USB device capable */
			      ufp_type, has_ufp, /* UFP modal support */
			      IDH_PTYPE_DFP_HOST, /* PDUSB host */
			      USB_TYPEC_RECEPTACLE, CONFIG_USB_VID);

	int idx = VDO_I(PRODUCT) + 1;

	if (has_ufp) {
		uint32_t ufp_cap = VDO_UFP1_CAPABILITY_USB20;

		if (usb32_drd || tbt_mode)
			ufp_cap |= VDO_UFP1_CAPABILITY_USB32;

#ifdef VDO_UFP1_CAPABILITY_USB4
		if (usb4_drd)
			ufp_cap |= VDO_UFP1_CAPABILITY_USB4;
#endif

		payload[idx++] = VDO_UFP1(ufp_cap, USB_TYPEC_RECEPTACLE,
					  tbt_mode ? VDO_UFP1_ALT_MODE_TBT3 : 0,
					  USB_R30_SS_U40_GEN3);
	}

	payload[idx++] = VDO_DFP(host_cap, USB_TYPEC_RECEPTACLE, port);

	return idx;
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
