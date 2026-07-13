# -*- makefile -*-
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Test-only common files

test_common-y += crc8.o
test_common-y += irq_locking.o
test_common-y += recursive_mutex.o
test_common-y += test_util.o
test_common-y += uart_buffering.o

test_common-$(CONFIG_PWM) += pwm.o
test_common-$(CONFIG_USB_PD_TCPC) += usb_pd_tcpc.o

# C++ files
# If we have both crc8.c and crc8.cc, we might need to be careful.
# In common/build.mk it was just crc8.o.
# If we need to support C++ compilation, Makefile.rules has rules for %.cc.
# We will see if it works.

_test_common_dir:=test/common/
include $(_test_common_dir)usbc/build.mk
$(eval $(call vars_from_dir,test_common,usbc,test_common-usbc))
