# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aic_flash_tests)

target_sources(app PRIVATE src/main.c)

zephyr_compile_definitions("CONFIG_ZEPHYR")
