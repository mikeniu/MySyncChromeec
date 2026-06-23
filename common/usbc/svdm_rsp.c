/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * SVDM response support.
 *
 * Built when CONFIG_SVDM_RSP is enabled and provides
 * responses to Structured VDM requests.
 */

#include "chipset.h"
#include "console.h"
#include "usb_mux.h"
#include "usb_pd.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

#define OPOS_TBT 1

static const union tbt_mode_resp_device vdo_tbt_modes[1] = { {
	.tbt_alt_mode = 0x0001,
	.tbt_adapter = TBT_ADAPTER_TBT3,
	.intel_spec_b0 = 0,
	.vendor_spec_b0 = 0,
	.vendor_spec_b1 = 0,
} };

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

static const uint32_t vdo_ufp1_tbt = VDO_UFP1(
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
	if (PD_VDO_VID(payload[VDO_INDEX_HDR]) != USB_SID_PD) {
		return 0; /* NAK */
	}

	payload[VDO_I(CSTAT)] = VDO_CSTAT(CONFIG_USB_PD_XID);
	payload[VDO_I(PRODUCT)] =
		VDO_PRODUCT(CONFIG_USB_PID, CONFIG_USB_BCD_DEV);

	if (pd_get_rev(port, TCPCI_MSG_SOP) == PD_REV30) {
		payload[VDO_I(PTYPE_UFP2_VDO)] = 0;

		/* PD Revision 3.0 */
		if (tbt_ufp_ack_allowed[port]) {
			payload[VDO_I(IDH)] = vdo_idh_rev30_tbt;
			payload[VDO_I(PTYPE_UFP1_VDO)] = vdo_ufp1_tbt;
			payload[VDO_I(PTYPE_DFP_VDO)] = vdo_dfp;

			return VDO_I(PTYPE_DFP_VDO) + 1;
		} else {
			payload[VDO_I(IDH)] = vdo_idh_rev30_no_ufp;
			payload[VDO_I(PRODUCT) + 1] = vdo_dfp;

			return VDO_I(PRODUCT) + 2;
		}
	}
	/* PD Revision 2.0 */
	if (tbt_ufp_ack_allowed[port]) {
		payload[VDO_I(IDH)] = vdo_idh_tbt;
	} else {
		payload[VDO_I(IDH)] = vdo_idh_no_ufp;
	}

	return VDO_I(PRODUCT) + 1;
}

static int svdm_tbt_compat_response_svids(int port, uint32_t *payload)
{
	/*
	 * For PD 3.1 compliance test TEST.PD.VDM.SRC.2,
	 * we should return NAK if we cannot recognized the incoming SVID.
	 */
	if (PD_VDO_VID(payload[0]) == USB_SID_PD && tbt_ufp_ack_allowed[port]) {
		payload[1] = VDO_SVID(USB_VID_INTEL, 0);
		return 2;
	} else {
		return 0; /* NAK */
	}
}

static int svdm_tbt_compat_response_modes(int port, uint32_t *payload)
{
	if (PD_VDO_VID(payload[0]) != USB_VID_INTEL ||
	    !tbt_ufp_ack_allowed[port]) {
		return 0; /* NAK */
	}
	for (int i = 0; i < ARRAY_SIZE(vdo_tbt_modes); i++) {
		payload[1 + i] = vdo_tbt_modes[i].raw_value;
	}

	return ARRAY_SIZE(vdo_tbt_modes) + 1;
}

__override enum ec_status
board_set_tbt_ufp_reply(int port, enum typec_tbt_ufp_reply reply)
{
	if (!IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE))
		return EC_RES_ERROR;
	/* Note: Host command has already bounds-checked port */
	if (reply == TYPEC_TBT_UFP_REPLY_ACK)
		tbt_ufp_ack_allowed[port] = true;
	else if (reply == TYPEC_TBT_UFP_REPLY_NAK)
		tbt_ufp_ack_allowed[port] = false;
	else
		return EC_RES_INVALID_PARAM;

	return EC_RES_SUCCESS;
}

static int svdm_tbt_compat_response_enter_mode(int port, uint32_t *payload)
{
	mux_state_t mux_state = 0;

	/* Do not enter mode while CPU is off. */
	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF))
		return 0; /* NAK */

	/* Do not enter mode while policy disallows it */
	if (!tbt_ufp_ack_allowed[port])
		return 0; /* NAK */

	if ((PD_VDO_VID(payload[0]) != USB_VID_INTEL) ||
	    (PD_VDO_OPOS(payload[0]) != OPOS_TBT))
		return 0; /* NAK */

	mux_state = usb_mux_get(port);
	/*
	 * Ref: USB PD 3.0 Spec figure 6-21 Successful Enter Mode sequence
	 * UFP (responder) should be in USB mode or safe mode before entering a
	 * Mode that requires the reconfiguring of any pins.
	 */
	if ((mux_state & USB_PD_MUX_USB_ENABLED) ||
	    (mux_state & USB_PD_MUX_SAFE_MODE)) {
#ifdef CONFIG_USB_PD_ALT_MODE_UFP
		pd_ufp_set_enter_mode(port, payload);
		set_tbt_compat_mode_ready(port);
#endif

		/*
		 * Ref: Above figure 6-21: UFP (responder) should be in the new
		 * mode before sending the ACK.  However, our mux set sequence
		 * may exceed tVDMEnterMode, so wait as long as we can
		 * before sending the reply without violating that timer.
		 */
		if (!usb_mux_set_completed(port))
			crec_usleep(PD_T_VDM_E_MODE / 2);

		CPRINTS("UFP Enter TBT mode");
		return 1; /* ACK */
	}

	CPRINTS("UFP failed to enter TBT mode(mux=0x%x)", mux_state);
	return 0;
}

__override const struct svdm_response svdm_rsp = {
	.identity = svdm_identity,
	.svids = &svdm_tbt_compat_response_svids,
	.modes = &svdm_tbt_compat_response_modes,
	.enter_mode = &svdm_tbt_compat_response_enter_mode,
	.amode = NULL,
	.exit_mode = NULL,
	/*
	 * Discover Identity support is required for devices with more than one
	 * DFP, but other SVDM commands are optional. We don't support operating
	 * as Responder in any mode, so leave them unimplemented. See 6.13.5,
	 * Applicability of Structured VDM Commands.
	 */
};
