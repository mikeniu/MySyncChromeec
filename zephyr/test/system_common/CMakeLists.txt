# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(system_common_test)

# Include FFF fakes
add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

target_include_directories(app PRIVATE
	include
	${PLATFORM_EC}/zephyr/shim/chip/npcx/include
)

FILE(GLOB test_sources src/*.c)
target_sources(app PRIVATE
	${test_sources}
	${PLATFORM_EC}/zephyr/shim/core/cortex-m/mpu.c
)
