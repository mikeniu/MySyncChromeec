# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Evaluates to the current directory name (e.g. "elan").
_cur_dir := $(notdir $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST)))))

host-util-bin-cxx-y += elaniap
elaniap-objs := $(addprefix $(_cur_dir)/, \
	iaptool.o \
	iapcontrol.o \
	filecontrol.o \
)
_elaniap_bin := $(out)/util/elaniap
$(_elaniap_bin): HOST_CFLAGS+=$(shell $(HOST_PKG_CONFIG) --cflags libusb-1.0)
$(_elaniap_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) \
	--libs --static libusb-1.0) -static
