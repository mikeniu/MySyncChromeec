# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Register the test binary as a host C++ utility
host-util-bin-cxx-y += elaniap_test

# Evaluates to the current directory name (e.g. "elan").
_cur_dir := $(notdir $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST)))))

# Define the objects required for the test binary
elaniap_test-objs := $(addprefix $(_cur_dir)/, \
	elaniap_test.o \
	iap_control.o \
	file_control.o \
)

_elaniap_test_bin := $(out)/util/elaniap_test

# Configure flags to pull in gtest and gmock via pkg-config
$(_elaniap_test_bin): HOST_CXXFLAGS+=-fvisibility=hidden -Iutil/elan
$(_elaniap_test_bin): HOST_CXXFLAGS+=$(shell $(HOST_PKG_CONFIG) \
                      --cflags libusb-1.0 gtest gmock)
$(_elaniap_test_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) \
                      --libs libusb-1.0 gtest gmock)
