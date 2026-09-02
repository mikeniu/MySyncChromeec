# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)

list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(cros_bt_passthru_test)

target_include_directories(app PRIVATE
	${PLATFORM_EC}/third_party/chre/include
	${PLATFORM_EC}/zephyr/subsys/bt_passthru
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_sources(app PRIVATE
	src/main.cc
	src/mock_hci_driver.c
	src/test_cros_hostcmd_transport.cc
)

target_link_libraries(app PRIVATE
	pw_span
	pw_status
	pw_function
)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
