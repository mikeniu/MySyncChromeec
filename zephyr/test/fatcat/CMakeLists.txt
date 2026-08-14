# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(fatcat)

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/fatcat/include")

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

add_subdirectory(tests/program)
add_subdirectory_ifdef(CONFIG_TEST_PROJECT_LAPIS tests/lapis)

set_compiler_property(APPEND PROPERTY coverage -O0)
