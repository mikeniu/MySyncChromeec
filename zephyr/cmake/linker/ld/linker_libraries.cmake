# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Zephyr cmake system looks into ${TOOLCHAIN_ROOT}, but we just send
# this out to the copy in ${ZEPHYR_BASE}.
include("${ZEPHYR_BASE}/cmake/linker/ld/linker_libraries.cmake")

if(NOT CONFIG_NATIVE_BUILD)
  # In general we don't want libc.
  message(WARNING "Disabling c_library")
  set_linker_property(PROPERTY c_library "")
endif()

if(CONFIG_PICOLIBC AND NOT CONFIG_PICOLIBC_USE_MODULE)
  # TODO(b/384559486) Fix this block
  # Add picolibc
  message(INFO "Setting c_library to picolibc install path")
  set_linker_property(PROPERTY c_library "${COREBOOT_SDK_ROOT}/picolibc/lib/libc.a")
  set_linker_property(PROPERTY c_library APPEND "${COREBOOT_SDK_ROOT}/picolibc/${CROSS_COMPILE_TARGET}/lib/libstdc++.a")
  set_linker_property(PROPERTY c_library APPEND "${COREBOOT_SDK_ROOT}/lib/gcc/${CROSS_COMPILE_TARGET}/14.2.0/libgcc.a")
endif()
