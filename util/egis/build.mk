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
    ec_comm.o \
)
_egis_bin := $(out)/util/et171_flash
$(_egis_bin): HOST_CFLAGS+=-fvisibility=hidden -Iutil/egis
$(_egis_bin): HOST_CFLAGS+=$(shell $(HOST_PKG_CONFIG) --cflags libusb-1.0 libcrypto)
$(_egis_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) --libs  libusb-1.0 libcrypto) -lpthread -ldl
