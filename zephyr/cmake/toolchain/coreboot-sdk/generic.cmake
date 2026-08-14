# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# generic.cmake is used for host-side compilation and preprocessing
# (e.g., for device-tree).  Thus, we should use LLVM for this
# actually, as that's what's currently supported compiler-wise in the
# chroot right now.
include("${TOOLCHAIN_ROOT}/cmake/toolchain/host/llvm/generic.cmake")

set(TOOLCHAIN_HAS_PICOLIBC ON CACHE BOOL "True if toolchain supports picolibc")

# Treat some DTS warnings as errors for boards. There is no -Eall flag.
# Find new warnings with
# zmake -D build -a --clobber |& egrep '\.dts.*Warning' | \
#   sed -e 's/.*Warning (//' -e 's/).*//' | sort -u
list(APPEND EXTRA_DTC_FLAGS
  # Enable these once they are all fixed.
  "-Eavoid_unnecessary_addr_size"
  # "-Egpios_property"
  # "-Ei2c_bus_reg"
  # "-Esimple_bus_reg"
  # "-Espi_bus_reg"
  # "-Eunique_unit_address"
  # "-Eunique_unit_address_if_enabled"
  # "-Eunit_address_format"
)
