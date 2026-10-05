# -*- makefile -*-
# Copyright 2014 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Host tools build
#

# See Makefile for description.
host-util-bin-cxx-y += ectool
host-util-bin-cxx-y += ec_parse_panicinfo
host-util-bin-cxx-y += lbplay
host-util-bin-cxx-y += stm32mon
ifneq ($(findstring -fsanitize=,$(HOST_CXXFLAGS)),-fsanitize=)
host-util-bin-cxx-y += stm32mon_static
endif
host-util-bin-cxx-y += lbcc
host-util-bin-cxx-y += iteflash
host-util-bin-cxx-y += itecomdbgr
host-util-bin-cxx-y += cbi-util

host-util-bin-cxx-y += rtkupdate
host-util-bin-cxx-y += uartupdatetool
uartupdatetool-objs=uut/main.o uut/cmd.o uut/opr.o uut/l_com_port.o \
	uut/lib_crc.o
$(out)/util/uartupdatetool: HOST_CFLAGS+=-Iutil/

# b/381916046: statically linked version of stm32mon.
stm32mon_static-objs = stm32mon.o
$(out)/util/stm32mon_static: HOST_LDFLAGS+=-static

# If the util/ directory in the private repo is symlinked into util/private,
# we want to build host-side tools from it, too.
ifneq ("$(wildcard util/private/build.mk)","")
include util/private/build.mk
endif
-include private/util_flags.mk

include util/egis/build.mk
include util/focaltech/build.mk
include util/elan/build.mk

comm-objs=$(util-lock-objs:%=lock/%) comm-host.o comm-dev.o
comm-objs+=comm-lpc.o comm-i2c.o misc_util.o comm-usb.o

iteflash-objs = iteflash.o usb_if.o
$(out)/util/iteflash: HOST_LDFLAGS+=$(LIBFTDIUSB_HOST_LDLIBS)
itecomdbgr-objs = itecomdbgr.o uart_utils.o
rtkupdate-objs = rtkupdate.o uart_utils.o
ectool-objs=ectool.o ectool_keyscan.o ec_flash.o $(comm-objs)
ectool-objs+=ectool_i2c.o
ectool-objs+=ectool_pdc_trace.o
ectool-objs+=ectool_pdc_pcap.o
ectool-objs+=../common/crc.o
lbplay-objs=lbplay.o $(comm-objs)
$(out)/util/lbplay: HOST_LDFLAGS+=$(LIBFTDIUSB_HOST_LDLIBS)

$(out)/util/ectool: HOST_LDFLAGS+=$(LIBEC_HOST_LDLIBS)

ec_parse_panicinfo-objs=ec_parse_panicinfo.o
$(out)/util/ec_parse_panicinfo: HOST_LDFLAGS+=$(LIBEC_HOST_LDLIBS)

cbi-util-objs=../common/crc8.o ../common/cbi.o
