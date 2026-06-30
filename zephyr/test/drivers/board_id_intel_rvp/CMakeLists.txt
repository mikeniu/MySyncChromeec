# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(board_id_intel_rvp)

target_sources(app PRIVATE
	${PLATFORM_EC}/zephyr/drivers/board_id/board_id_intel_rvp.c)

target_sources(app PRIVATE src/board_id_intel_rvp.c)
