# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(bluey)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories(
	"${PLATFORM_EC_PROGRAM_DIR}/bluey/include"
	include)

add_subdirectory_ifdef(CONFIG_TEST_BOARD_BASEBOARD tests/baseboard)
add_subdirectory_ifdef(CONFIG_TEST_BOARD_BLUEY tests/bluey)
add_subdirectory_ifdef(CONFIG_TEST_BOARD_QUENBI tests/quenbi)
add_subdirectory_ifdef(CONFIG_TEST_BOARD_MICA tests/mica)
add_subdirectory_ifdef(CONFIG_TEST_BOARD_QUARTZ tests/quartz)
add_subdirectory_ifdef(CONFIG_TEST_BOARD_ANNITE tests/annite)
add_subdirectory_ifdef(CONFIG_TEST_BOARD_PIC tests/pic)
