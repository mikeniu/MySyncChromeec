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
test-list-host += cbi
test-list-host += charge_ramp
test-list-host += console_edit
test-list-host += crc
test-list-host += fpsensor_auth_commands
test-list-host += fpsensor_auth_commands_otp
test-list-host += fpsensor_auth_crypto_stateful
test-list-host += fpsensor_auth_crypto_stateless
test-list-host += fpsensor_crypto
test-list-host += fpsensor_crypto_with_mock
test-list-host += fpsensor_crypto_with_mock_otp
test-list-host += fpsensor_state
test-list-host += host_command
test-list-host += kb_8042
test-list-host += math_util
test-list-host += motion_angle
test-list-host += motion_angle_tablet
test-list-host += motion_lid
test-list-host += motion_sense_fifo
test-list-host += power_button
test-list-host += queue
test-list-host += rollback_secret
test-list-host += rsa3
test-list-host += rtc
test-list-host += sbs_charging
test-list-host += sha256
test-list-host += sha256_unrolled
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
cbi-y=cbi.o
charge_ramp-y+=charge_ramp.o
console_edit-y=console_edit.o
crc-y=crc.o
fpsensor_auth_commands-y=fpsensor_auth_commands.o
fpsensor_auth_commands_otp-$(rw-test)=fpsensor_auth_commands_otp.o
fpsensor_auth_crypto_stateful-y=fpsensor_auth_crypto_stateful.o
fpsensor_auth_crypto_stateless-y=fpsensor_auth_crypto_stateless.o
fpsensor_crypto-y=fpsensor_crypto.o
fpsensor_crypto_with_mock-y=fpsensor_crypto_with_mock.o
fpsensor_crypto_with_mock_otp-y=fpsensor_crypto_with_mock_otp.o
fpsensor_state-y=fpsensor_state.o
host_command-y=host_command.o
kb_8042-y=kb_8042.o
math_util-y=math_util.o
motion_angle-y=motion_angle.o motion_angle_data_literals.o motion_common.o
motion_angle_tablet-y=motion_angle_tablet.o motion_angle_data_literals_tablet.o motion_common.o
motion_lid-y=motion_lid.o
motion_sense_fifo-y=motion_sense_fifo.o
power_button-y=power_button.o
queue-y=queue.o
rollback_secret-y=rollback_secret.o
rsa3-y=rsa.o
rtc-y=rtc.o
sbs_charging-y=sbs_charging.o
sha256-y=sha256.o
sha256_unrolled-y=sha256.o
