# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Test-only USBC files

test_common-usbc-$(CONFIG_USB_VPD) += usb_pe_ctvpd_sm.o
test_common-usbc-$(CONFIG_USB_CTVPD) += usb_pe_ctvpd_sm.o
