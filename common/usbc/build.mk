# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build for USB Type-C and Power Delivery

ifneq ($(CONFIG_USB_PD_TCPMV2),)
common-usbc-$(CONFIG_USB_PD_TCPMV2) += usb_pd_timer.o usb_sm.o usbc_task.o

# Type-C state machines
ifneq ($(CONFIG_USB_TYPEC_SM),)
common-usbc-$(CONFIG_USB_DRP_ACC_TRYSRC) += usb_tc_drp_acc_trysrc_sm.o
endif # CONFIG_USB_TYPEC_SM

# Protocol state machine
common-usbc-$(CONFIG_USB_PD_TCPMV2) += usb_prl_sm.o

# Policy Engine state machines
common-usbc-$(CONFIG_USB_DRP_ACC_TRYSRC) += usbc_pd_policy.o
common-usbc-$(CONFIG_USB_DRP_ACC_TRYSRC) += usb_pe_drp_sm.o
common-usbc-$(CONFIG_USB_DRP_ACC_TRYSRC) += usb_pd_dpm.o
common-usbc-$(CONFIG_USB_PD_DP_MODE) += dp_alt_mode.o
common-usbc-$(CONFIG_USB_PD_DP_HPD_GPIO) += dp_hpd_gpio.o
common-usbc-$(CONFIG_USB_PD_TBT_COMPAT_MODE) += tbt_alt_mode.o
common-usbc-$(CONFIG_USB_PD_USB4) += usb_mode.o
common-usbc-$(CONFIG_CMD_PD) += usb_pd_console.o
common-usbc-$(CONFIG_USB_PD_HOST_CMD) += usb_pd_host.o

# Retimer firmware update
common-usbc-$(CONFIG_USBC_RETIMER_FW_UPDATE) += usb_retimer_fw_update.o

# ALT-DP mode for UFP ports
common-usbc-$(CONFIG_USB_PD_ALT_MODE_UFP_DP) += usb_pd_dp_ufp.o
endif # CONFIG_USB_PD_TCPMV2

# For testing
common-usbc-$(CONFIG_TEST_USB_PD_TIMER) += usb_pd_timer.o
common-usbc-$(CONFIG_TEST_SM) += usb_sm.o

# SVDM response support
common-usbc-$(CONFIG_SVDM_RSP) += svdm_rsp.o
