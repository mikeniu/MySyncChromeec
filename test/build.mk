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
test-list-host += battery_config
test-list-host += bklight_passthru
test-list-host += boringssl_crypto
test-list-host += charge_ramp
test-list-host += fpsensor_auth_commands
test-list-host += fpsensor_auth_commands_otp
test-list-host += fpsensor_auth_crypto_stateful
test-list-host += fpsensor_auth_crypto_stateless
test-list-host += fpsensor_crypto
test-list-host += fpsensor_crypto_with_mock
test-list-host += fpsensor_crypto_with_mock_otp
test-list-host += fpsensor_state
test-list-host += host_command
test-list-host += math_util

test-list-host += sbs_charging
# TODO(b/237823627): When building for the host, we're linking against the
# toolchain's C standard library, so these tests are actually testing the
# toolchain's C standard library.
test-list-host += thermal
test-list-host += usb_pd_timer
test-list-host += usb_sm_framework_h3
test-list-host += usb_typec_drp_acc_trysrc
test-list-host += usb_tcpmv2_compliance
test-list-host += usb_prl
test-list-host += usb_pe_drp
test-list-host += usb_pe_drp_noextended
test-list-host += x25519
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

battery_config-y=battery_config.o
bklight_passthru-y=bklight_passthru.o
boringssl_crypto-y=boringssl_crypto.o
charge_ramp-y+=charge_ramp.o
fpsensor_auth_commands-y=fpsensor_auth_commands.o
fpsensor_auth_commands_otp-$(rw-test)=fpsensor_auth_commands_otp.o
fpsensor_auth_crypto_stateful-y=fpsensor_auth_crypto_stateful.o
fpsensor_auth_crypto_stateless-y=fpsensor_auth_crypto_stateless.o
fpsensor_crypto-y=fpsensor_crypto.o
fpsensor_crypto_with_mock-y=fpsensor_crypto_with_mock.o
fpsensor_crypto_with_mock_otp-y=fpsensor_crypto_with_mock_otp.o
fpsensor_state-y=fpsensor_state.o
host_command-y=host_command.o
math_util-y=math_util.o

sbs_charging-y=sbs_charging.o
thermal-y=thermal.o
usb_pd_timer-y=usb_pd_timer.o
usb_sm_framework_h3-y=usb_sm_framework_h3.o
usb_typec_vpd-y=usb_typec_ctvpd.o vpd_api.o usb_sm_checks.o fake_usbc.o
usb_typec_ctvpd-y=usb_typec_ctvpd.o vpd_api.o usb_sm_checks.o fake_usbc.o
usb_typec_drp_acc_trysrc-y=usb_typec_drp_acc_trysrc.o vpd_api.o \
	usb_sm_checks.o
usb_prl_old-y=usb_prl_old.o usb_sm_checks.o fake_usbc.o
usb_prl-y=usb_prl.o usb_sm_checks.o
usb_prl_noextended-y=usb_prl_noextended.o usb_sm_checks.o fake_usbc.o
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
x25519-y=x25519.o
