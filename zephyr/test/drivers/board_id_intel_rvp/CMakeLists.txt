# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(board_id_intel_rvp)

target_sources(app PRIVATE
	${PLATFORM_EC}/zephyr/drivers/board_id/board_id_intel_rvp.c)

# Shared helpers and fakes used by every board-id test suite.
target_sources(app PRIVATE src/board_id_test_common.c)

target_sources_ifdef(CONFIG_LINK_TEST_SUITE_BOARD_ID_INTEL_RVP
	app PRIVATE src/board_id_intel_rvp.c)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_BOARD_ID_INTEL_RVP_NOT_READY
	app PRIVATE src/board_id_not_ready.c)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_BOARD_ID_INTEL_RVP_S5_CALLBACK
	app PRIVATE src/board_id_s5_callback.c)
