# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

cros_ec_library_include_directories(include)

zephyr_library_sources("src/scp.c")

if(DEFINED CONFIG_BOARD_NAVI_SCP_ZEPHYR)
	project(navi-scp-zephyr)
elseif(DEFINED CONFIG_BOARD_HYLIA_SCP_ZEPHYR)
	project(hylia-scp-zephyr)
endif()
