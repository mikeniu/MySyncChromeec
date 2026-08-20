# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Evaluates to the current directory name (e.g. "egis").
_cur_dir := $(notdir $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST)))))

host-util-bin-cxx-y += et171_flash
et171_flash-objs := $(addprefix $(_cur_dir)/, \
    et171_flash.o \
    bootrom.o \
    usb_comm.o \
    crypto_util.o \
    cli_options.o \
    file_util.o \
    flasher_logic.o \
)
_egis_bin := $(out)/util/et171_flash
$(_egis_bin): HOST_CXXFLAGS+=-fvisibility=hidden -Iutil/egis
$(_egis_bin): HOST_CXXFLAGS+=$(shell $(HOST_PKG_CONFIG) \
    --cflags libusb-1.0 boringssl)
$(_egis_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) \
    --libs boringssl libusb-1.0)

host-util-bin-cxx-y += et171_flash_test

et171_flash_test-objs := $(addprefix $(_cur_dir)/, \
    bootrom.o bootrom_test.o \
    usb_comm.o usb_comm_test.o \
    crypto_util.o crypto_util_test.o \
    cli_options.o cli_options_test.o \
    file_util.o file_util_test.o \
    flasher_logic.o flasher_logic_test.o \
    egis_util_test.o \
)

_egis_test_bin := $(out)/util/et171_flash_test

_EGIS_TEST_PKGS := libusb-1.0 boringssl gtest gtest_main

$(_egis_test_bin): HOST_CXXFLAGS+=-fvisibility=hidden -Iutil/egis
$(_egis_test_bin): HOST_CXXFLAGS+=$(shell $(HOST_PKG_CONFIG) --cflags \
	$(_EGIS_TEST_PKGS))
$(_egis_test_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) --libs \
	$(_EGIS_TEST_PKGS))

.PHONY: egis-utils-test
egis-utils-test: $(_egis_test_bin)
	@$(_egis_test_bin)

utils-test: egis-utils-test
