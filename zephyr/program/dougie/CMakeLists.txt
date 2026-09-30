# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY "src/battery_present.c")

if(DEFINED CONFIG_BOARD_BANDIT)
	project(dougie)
	add_subdirectory(bandit)
endif()
