# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(chre_test)

target_include_directories(app PRIVATE
  ${PLATFORM_EC}/zephyr/shim/include
  mock_chre
)

target_sources(app PRIVATE
  src/main.cc
  src/sensor_pal_test.cc
  src/tagged_log_test.cc
  mock_chre/mock_chre.cc
  ${PLATFORM_EC}/zephyr/shim/src/chre.cc
  ${PLATFORM_EC}/zephyr/shim/src/chre_sensor_pal.cc
)
