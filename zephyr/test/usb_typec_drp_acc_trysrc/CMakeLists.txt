# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(usb_typec_drp_acc_trysrc)

target_sources(app PRIVATE
	src/usb_typec_drp_acc_trysrc_shim.c
	src/test_usb_typec_drp_acc_trysrc.c
)

target_include_directories(app PRIVATE
	${PLATFORM_EC}/include
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
