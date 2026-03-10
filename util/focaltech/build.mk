# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Register the test binary as a host C++ utility
_cur_dir := $(notdir $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST)))))
host-util-bin-cxx-y += focaltech_test

# Define the objects required for the test binary
focaltech_test-objs := $(addprefix $(_cur_dir)/, \
	focaltech_test.o \
	usb_context.o \
	ft_scsi.o \
	updater.o \
)

_focaltech_test_bin := $(out)/util/focaltech_test



# Configure flags to pull in gtest and gmock via pkg-config
$(_focaltech_test_bin): HOST_CXXFLAGS+=-fvisibility=hidden -Iutil/focaltech
$(_focaltech_test_bin): HOST_CXXFLAGS+=$(shell $(HOST_PKG_CONFIG) --cflags libusb-1.0 boringssl gtest gmock)
$(_focaltech_test_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) --libs  libusb-1.0 boringssl gtest gmock) 

host-util-bin-cxx-y += focaltech_fingerprint_iap
focaltech_fingerprint_iap-objs := $(addprefix $(_cur_dir)/, \
	usb_context.o \
	ft_scsi.o \
	updater.o \
	main.o \
)

_focaltech_bin := $(out)/util/focaltech_fingerprint_iap
$(_focaltech_bin): HOST_CFLAGS+=-fvisibility=hidden
$(_focaltech_bin): HOST_CFLAGS+=$(shell $(HOST_PKG_CONFIG) --cflags libusb-1.0)
$(_focaltech_bin): HOST_CFLAGS+=$(shell $(HOST_PKG_CONFIG) --cflags boringssl)
$(_focaltech_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) --libs  libusb-1.0 )
$(_focaltech_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) --libs  boringssl )
