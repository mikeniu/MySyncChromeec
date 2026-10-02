# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(it8xxx2_shi)

target_sources(app PRIVATE
	src/main.c
	${PLATFORM_EC}/zephyr/shim/chip/it8xxx2/shi.c
)
