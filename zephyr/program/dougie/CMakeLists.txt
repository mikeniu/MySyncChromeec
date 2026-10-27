# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

if(DEFINED CONFIG_BOARD_BANDIT)
	project(dougie)
	add_subdirectory(bandit)
endif()

zephyr_library_sources("src/chipset.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_CONTROLLER
			     "src/dp_alt_mode.c")
