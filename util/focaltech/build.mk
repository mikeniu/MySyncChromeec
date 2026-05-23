# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Evaluates to the current directory name (e.g. "focaltech").
_cur_dir := $(notdir $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST)))))

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
$(_focaltech_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) --libs --static libusb-1.0 ) -static
$(_focaltech_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) --libs --static boringssl) -static
