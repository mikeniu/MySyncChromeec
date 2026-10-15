# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if(CONFIG_ISH_CHRE)
  pw_add_library(chre.platform.ec.log INTERFACE
    HEADERS
      shim/include/chre/target_platform/log.h
    PUBLIC_INCLUDES
      shim/include
  )
  pw_zephyrize_libraries_ifdef(CONFIG_ISH_CHRE
    chre.platform.ec.log
  )
  if(DEFINED chre.platform.log_BACKEND)
    pw_set_backend(chre.platform.log chre.platform.ec.log)
  endif()

  target_sources(app PRIVATE shim/src/chre.cc)
  if(NOT CONFIG_ZTEST)
    set(CHRE_PLATFORM_PIGWEED ON CACHE BOOL "" FORCE)
    zephyr_compile_options(-include zephyr/sys/util_macro.h)
    add_subdirectory(${CHRE_ROOT} chre)
    target_link_libraries(app PRIVATE
      chre
      pw_allocator_zephyr.synchronized_heap_allocator
    )
    if(CONFIG_CHRE_TIMER_WORLD)
      target_link_libraries(app PRIVATE chre.apps.timer_world)
    endif()
    if(CONFIG_CHRE_HELLO_WORLD)
      target_link_libraries(app PRIVATE chre.apps.hello_world)
    endif()
  endif()
endif()
