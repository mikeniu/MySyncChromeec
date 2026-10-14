# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if(CONFIG_ISH_CHRE OR CONFIG_SCP_CHRE)
  pw_add_library(chre.platform.ec.log INTERFACE
    HEADERS
      shim/include/chre/target_platform/log.h
    PUBLIC_INCLUDES
      shim/include
  )
  pw_zephyrize_libraries_ifdef(CONFIG_ISH_CHRE
    chre.platform.ec.log
  )
  pw_zephyrize_libraries_ifdef(CONFIG_SCP_CHRE
    chre.platform.ec.log
  )
  if(DEFINED chre.platform.log_BACKEND)
    pw_set_backend(chre.platform.log chre.platform.ec.log)
  endif()

  target_sources(app PRIVATE shim/src/chre.cc)
  if(NOT CONFIG_ZTEST)
    if(CONFIG_SCP_CHRE AND CONFIG_PLATFORM_EC_MOTIONSENSE)
      include(${CHRE_ROOT}/pal/backend.cmake)
      pw_add_library(chre.platform.ec.pal_sensor INTERFACE)
      pw_set_backend(chre.pal.sensor chre.platform.ec.pal_sensor)
      target_sources(app PRIVATE shim/src/chre_sensor_pal.cc)
    endif()
    set(CHRE_PLATFORM_PIGWEED ON CACHE BOOL "" FORCE)
    zephyr_compile_options(
      -include zephyr/sys/util_macro.h
      $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
    )
    add_subdirectory(${CHRE_ROOT} chre EXCLUDE_FROM_ALL)
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
    if(CONFIG_CHRE_SENSOR_WORLD)
      target_link_libraries(app PRIVATE chre.apps.sensor_world)
    endif()
    if(CONFIG_CHRE_MESSAGE_WORLD)
      target_link_libraries(app PRIVATE chre.apps.message_world)
    endif()
  endif()
endif()
