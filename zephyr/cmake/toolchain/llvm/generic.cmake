# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(COMPILER clang)
set(LINKER lld)
set(BINTOOLS llvm)

if("${ARCH}" STREQUAL "posix")
  set(LINKER ld)
endif()

# Only set these if we are using the LLVM toolchain for the target build.
# This prevents other toolchains (like coreboot-sdk) that include this file
# for preprocessing from incorrectly inheriting LLVM-specific capability flags.
if(ZEPHYR_TOOLCHAIN_VARIANT STREQUAL "llvm" OR TOOLCHAIN_VARIANT_COMPILER STREQUAL "llvm")
  if("${ARCH}" STREQUAL "posix")
    set(TOOLCHAIN_HAS_NEWLIB OFF CACHE BOOL "True if toolchain supports newlib")
  else()
    set(TOOLCHAIN_HAS_NEWLIB ON CACHE BOOL "True if toolchain supports newlib")
  endif()

  set(TOOLCHAIN_HAS_LIBCXX ON CACHE BOOL "True if toolchain supports libc++")

  set(TOOLCHAIN_VARIANT_COMPILER llvm CACHE STRING "Variant compiler being used")
endif()
