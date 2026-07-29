# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(fingerprint_algorithm_ft98xx)

FILE(GLOB app_sources src/*.c)

target_sources(app PRIVATE
    ${app_sources}
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../subsys/fingerprint/alg/ft98xx_bio_alg.c"
)

zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

# Enable private algorithm path for unit test mocking
target_compile_definitions(app PRIVATE
    CONFIG_HAVE_FT_LOCKER_PRIVATE_ALGORITHM=1
)
