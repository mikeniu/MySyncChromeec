<<<<<<< PATCH SET (1453e788f70905e1537b7037445d9018459c2ba0 Convert the code to C++.  Remove USB suspend/resume function)
<<<<<<< PATCH SET (1cc14ffb3bc0a588c4cc1a841d11691cd38be838 Convert the code to C++.  Remove USB suspend/resume function)
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
||||||| BASE      (f0e1f5401782cba4cabda434dcc978aec0463cf4 util: focaltech: Add focaltech IAP tool)
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Evaluates to the current directory name (e.g. "focaltech").
_cur_dir := $(notdir $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST)))))

host-util-bin-y += focaltech_fingerprint_iap
focaltech_fingerprint_iap-objs := $(addprefix $(_cur_dir)/, \
	focaltech/src/ft_usb_port.o \
	focaltech/src/ff_util.o \
	focaltech/src/ft_sg_io.o \
	focaltech/src/net_usb.o \
	focaltech/src/fun.o \
	focaltech/src/main.o \
)

_focaltech_bin := $(out)/util/focaltech_fingerprint_iap
$(_focaltech_bin): HOST_CFLAGS+=-fvisibility=hidden -DHAVE_CONFIG_H -Iutil/focaltech/focaltech/include
$(_focaltech_bin): HOST_CFLAGS+=$(shell $(HOST_PKG_CONFIG) --cflags libusb-1.0)
$(_focaltech_bin): HOST_CFLAGS+=$(shell $(HOST_PKG_CONFIG) --cflags boringssl)
$(_focaltech_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) --libs --static libusb-1.0 ) -static
$(_focaltech_bin): HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) --libs --static boringssl) -static
=======
>>>>>>> BASE      (15158824c8a1973786840dac479eeb84a7cc33a8 ec: fix missing includes)
||||||| BASE      (15158824c8a1973786840dac479eeb84a7cc33a8 ec: fix missing includes)
=======
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
>>>>>>> BASE      (aea89f2ea6978aa9a7fe54dd61b110e76f30dea8 util: focaltech: Add focaltech IAP tool)
