# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(rts5817maa_test)

# Enable private driver path globally for both app and driver libraries
zephyr_compile_definitions(CONFIG_HAVE_RTS5817MAA_PRIVATE_DRIVER=1)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

# Add fingerprint drivers directory to include fingerprint_rts5817maa.h
target_include_directories(app PUBLIC ${PLATFORM_EC}/zephyr/drivers/fingerprint)
