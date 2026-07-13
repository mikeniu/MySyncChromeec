# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

_cur_dir := $(notdir $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST)))))

host-util-bin-cxx-y += elaniap_tool
elaniap_tool-objs := $(addprefix $(_cur_dir)/, \
	iap_tool.o \
	cli.o \
	iap_control.o \
	file_control.o \
	libusb_backend.o \
)

_elaniap_tool_bin := $(out)/util/elaniap_tool
$(_elaniap_tool_bin): HOST_CXXFLAGS+=-Iutil/elan
$(_elaniap_tool_bin): HOST_CXXFLAGS+=$(shell $(HOST_PKG_CONFIG) \
           --cflags libusb-1.0)
$(_elaniap_tool_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) \
           --libs libusb-1.0)

host-util-bin-cxx-y += elaniap_test_runner
elaniap_test_runner-objs := $(addprefix $(_cur_dir)/, \
	cli_test.o \
	file_control_test.o \
	iap_control_test.o \
	utility_test.o \
	cli.o \
	file_control.o \
	iap_control.o \
)

_elaniap_test_bin := $(out)/util/elaniap_test_runner
$(_elaniap_test_bin): HOST_CXXFLAGS+=-Iutil/elan
$(_elaniap_test_bin): HOST_CXXFLAGS+=$(shell $(HOST_PKG_CONFIG) \
           --cflags gtest gmock)
$(_elaniap_test_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) \
           --libs gtest gtest_main gmock)

.PHONY: elan-utils-test
elan-utils-test: $(_elaniap_test_bin)
	@$(_elaniap_test_bin)

utils-test: elan-utils-test
