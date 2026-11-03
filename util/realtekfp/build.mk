# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

_cur_dir := $(notdir $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST)))))

host-util-bin-cxx-y += rts5817_flash_test
rts5817_flash_test-objs := $(addprefix $(_cur_dir)/, \
	rts5817_usb_device.o \
	rts5817_usb_device_test.o \
	rts5817_flash_cli.o \
	rts5817_flash_cli_test.o \
)

_rts5817_test_bin := $(out)/util/rts5817_flash_test
$(_rts5817_test_bin): HOST_CXXFLAGS+=-Iutil/realtekfp
$(_rts5817_test_bin): HOST_CXXFLAGS+=$(shell $(HOST_PKG_CONFIG) \
	--cflags gtest gmock)
$(_rts5817_test_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) \
	--libs gtest gtest_main gmock)

.PHONY: rts5817-utils-test
rts5817-utils-test: $(_rts5817_test_bin)
	@$(_rts5817_test_bin)

utils-test: rts5817-utils-test
