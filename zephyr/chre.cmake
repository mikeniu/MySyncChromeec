# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if(CONFIG_ISH_CHRE)
  if(NOT CONFIG_ZTEST)
    set(CHRE_PLATFORM_PIGWEED ON CACHE BOOL "" FORCE)
    zephyr_compile_options(-include zephyr/sys/util_macro.h)
    add_subdirectory(${CHRE_ROOT} chre)
    target_link_libraries(app PRIVATE chre)
  endif()
endif()
