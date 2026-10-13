# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if(CONFIG_ISH_CHRE)
  zephyr_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>)

  if(NOT CONFIG_ZTEST)
    set(CHRE_PLATFORM_PIGWEED ON CACHE BOOL "" FORCE)
    zephyr_compile_options(-include zephyr/sys/util_macro.h)
    add_compile_definitions(CHRE_ATOMIC_UINT32_REF_ENABLED=1)
    include(${CHRE_ROOT}/chre_api/backend.cmake)
    include(${CHRE_ROOT}/platform/shared/backend.cmake)
    include(${CHRE_ROOT}/test/backend.cmake)
    pw_add_library(chre.third_party.flatbuffers INTERFACE
      HEADERS
        ${CHRE_ROOT}/external/flatbuffers/include/flatbuffers/base.h
        ${CHRE_ROOT}/external/flatbuffers/include/flatbuffers/flatbuffers.h
        ${CHRE_ROOT}/external/flatbuffers/include/flatbuffers/stl_emulation.h
      PUBLIC_INCLUDES
        ${CHRE_ROOT}/external/flatbuffers/include
    )
    set(chre_third_party_flatbuffers_LIBRARY chre.third_party.flatbuffers
        CACHE STRING "" FORCE)
    add_subdirectory(${CHRE_ROOT} chre)
    target_link_libraries(app PRIVATE chre)
  endif()
endif()
