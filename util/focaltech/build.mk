# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

_cur_dir := $(notdir $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST)))))

host-util-bin-cxx-y += focaltech_iap
focaltech_iap-objs := $(addprefix $(_cur_dir)/, \
	ft_util.o \
	libusb_transport.o \
	ft_scsi.o \
	updater.o \
	cli.o \
	main.o \
)

_focaltech_bin := $(out)/util/focaltech_iap
$(_focaltech_bin): HOST_CXXFLAGS+=-fvisibility=hidden -DFT_LOG_COLOR_EN
$(_focaltech_bin): HOST_CXXFLAGS+=$(shell $(HOST_PKG_CONFIG) --cflags \
	libusb-1.0 boringssl)
$(_focaltech_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) --libs \
	libusb-1.0 boringssl)

host-util-bin-cxx-y += focaltech_test
focaltech_test-objs := $(addprefix $(_cur_dir)/, \
	ft_util.o \
	ft_util_test.o \
	ft_help_test.o \
	ft_scsi_test.o \
	cli_test.o \
	usb_device_test.o \
	updater_test.o \
	ft_scsi.o \
	cli.o \
	updater.o \
)

_focaltech_test_bin := $(out)/util/focaltech_test
$(_focaltech_test_bin): HOST_CXXFLAGS+=-fvisibility=hidden \
	-Iutil/focaltech -DFT_LOG_DIS
$(_focaltech_test_bin): HOST_CXXFLAGS+=$(shell $(HOST_PKG_CONFIG) --cflags \
	libusb-1.0 boringssl gtest gmock)
$(_focaltech_test_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) --libs \
	libusb-1.0 boringssl gtest gmock gtest_main)

.PHONY: focaltech-utils-test
focaltech-utils-test: $(_focaltech_test_bin)
	@$(_focaltech_test_bin)
utils-test: focaltech-utils-test
