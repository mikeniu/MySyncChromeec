# -*- makefile -*-
# Copyright 2013 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Device test binaries
test-list-y ?= stdlib

# Emulator tests
ifneq ($(TEST_LIST_HOST),)
test-list-host=$(TEST_LIST_HOST)
else
test-list-host =


# TODO(b/237823627): When building for the host, we're linking against the
# toolchain's C standard library, so these tests are actually testing the
# toolchain's C standard library.
test-list-host += usb_sm_framework_h3
test-list-host += usb_typec_drp_acc_trysrc
test-list-host += usb_tcpmv2_compliance
test-list-host += usb_pe_drp
test-list-host += usb_pe_drp_noextended
endif

# Build up the list of coverage test targets based on test-list-host, but
# with some tests excluded because they cause code coverage to fail.

cov-dont-test =

cov-test-list-host = $(filter-out $(cov-dont-test), $(test-list-host))

rw-test = rw
ifeq ($(and $(BOARD_HOST),$(TEST_BUILD)),y)
# TODO(b/346616972): The "emulator" (TEST_BUILD=y with BOARD=host) runs the
# tests from the RO image, so we need to build for RO.
rw-test = ro
endif


sbs_charging-y=sbs_charging.o
usb_sm_framework_h3-y=usb_sm_framework_h3.o
usb_typec_drp_acc_trysrc-y=usb_typec_drp_acc_trysrc.o vpd_api.o \
	usb_sm_checks.o
usb_pe_drp_old-y=usb_pe_drp_old.o usb_sm_checks.o
usb_pe_drp_old_noextended-y=usb_pe_drp_old_noextended.o usb_sm_checks.o
usb_pe_drp-y=usb_pe_drp.o usb_sm_checks.o
usb_pe_drp_noextended-y=usb_pe_drp_noextended.o usb_sm_checks.o
usb_tcpmv2_compliance-y=usb_tcpmv2_compliance.o usb_tcpmv2_compliance_common.o \
	usb_tcpmv2_td_pd_ll_e3.o \
	usb_tcpmv2_td_pd_ll_e4.o \
	usb_tcpmv2_td_pd_ll_e5.o \
	usb_tcpmv2_td_pd_src_e1.o \
	usb_tcpmv2_td_pd_src_e2.o \
	usb_tcpmv2_td_pd_src_e5.o \
	usb_tcpmv2_td_pd_src3_e1.o \
	usb_tcpmv2_td_pd_src3_e7.o \
	usb_tcpmv2_td_pd_src3_e8.o \
	usb_tcpmv2_td_pd_src3_e9.o \
	usb_tcpmv2_td_pd_src3_e26.o \
	usb_tcpmv2_td_pd_src3_e32.o \
	usb_tcpmv2_td_pd_snk3_e12.o \
	usb_tcpmv2_td_pd_vndi3_e3.o \
	usb_tcpmv2_td_pd_other.o \
	test_battery_mock.o
