# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Initialize Zephyr
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rauru_usbc_tests)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories(
	"${PLATFORM_EC_PROGRAM_DIR}/rauru/include"
	include)

target_compile_definitions(app PRIVATE CHARGER_SOLO=0 CONFIG_PLATFORM_EC_CHARGER_BQ25720=1)

target_sources(app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/rauru/src/common.c
	${PLATFORM_EC_PROGRAM_DIR}/rauru/src/usbc.c
	${PLATFORM_EC_PROGRAM_DIR}/rauru/src/dp_alt_mode.c
	${PLATFORM_EC_PROGRAM_DIR}/rauru/src/hdmi.c
	${PLATFORM_EC_PROGRAM_DIR}/rauru/src/hooks.c
	${PLATFORM_EC_PROGRAM_DIR}/rauru/src/hibernate.c
	${PLATFORM_EC_PROGRAM_DIR}/rauru/src/sub_board.c
	${PLATFORM_EC_PROGRAM_DIR}/rauru/navi/src/suspend_resume.c
	${PLATFORM_EC_PROGRAM_DIR}/rauru/navi/src/usb_mux.c
	${PLATFORM_EC}/driver/retimer/ps8802.c
	${PLATFORM_EC}/driver/usb_mux/anx3443.c)

# Add the test source
target_sources(app PRIVATE
	src/main.c
	src/usbc_test.c
	src/dp_alt_mode_test.c
	src/hooks_test.c
	src/navi_test.c
	src/retimer_mux_test.c
)
