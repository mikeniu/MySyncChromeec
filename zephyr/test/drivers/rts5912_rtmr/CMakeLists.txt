# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(rts5912_rtmr_test)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories(
	"${ZEPHYR_BASE}/soc/realtek/ec/rts5912"
)

target_compile_options(app PRIVATE
	-include stdio.h
	-DBBRAM_WP_FLAG_INVALID=0xFFFFFFFF
)

target_sources(app PRIVATE
	src/rts5912_rtmr_test.c
	${PLATFORM_EC}/zephyr/drivers/cros_system/cros_system_rtk.c
)
