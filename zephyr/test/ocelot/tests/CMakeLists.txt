# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_include_directories(ocelot/src/)

if(CONFIG_TEST_PROJECT_OCELOTRVP_NPCX)

# Add sources for ocelotrvp-npcx board.
target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/ocelot/src/rvp_board_power.c)

# Common fake functions
target_sources(app PRIVATE ocelot/src/fakes.c)
endif()

target_sources_ifdef(CONFIG_TEST_PROJECT_OCELOT_BOARD_POWER app PRIVATE power/board_power.c)
