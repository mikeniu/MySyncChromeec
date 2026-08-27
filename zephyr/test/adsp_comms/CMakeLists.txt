# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(adsp_comms)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories(
	"${PLATFORM_EC}/include"
	"${PLATFORM_EC}/zephyr/include"
	"${PLATFORM_EC}/zephyr/subsys/adsp_comms"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)

zephyr_compile_definitions(CONFIG_TEST_DISABLE_INLINE_CHIPSET_IN_STATE)

target_sources(app PRIVATE
	src/stubs.c
	src/test_adsp_i2c_mode.c
	src/test_adsp_comms.c
)
