# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(chre_test)

target_include_directories(app PRIVATE
  ${PLATFORM_EC}/include
  ${PLATFORM_EC}/test
  ${PLATFORM_EC}/zephyr/shim/include
  ${PLATFORM_EC}/zephyr/subsys/chre/include
  ${PLATFORM_EC}/zephyr/test/test_utils
  mock_chre
)

target_link_libraries(app PRIVATE pw_log)
target_compile_definitions(app PRIVATE CONFIG_PLATFORM_EC_PRESERVED_END_OF_RAM_SIZE=1024)

target_sources(app PRIVATE
  src/main.cc
  src/tagged_log_test.cc
  mock_chre/mock_chre.cc
)
