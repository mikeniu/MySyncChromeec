# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(qcom_ap_pwrseq)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

target_sources_ifdef(CONFIG_TEST_DEFAULT app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_TEST_EC_APP_MAIN app PRIVATE src/test_ec_app_main.c)
target_sources_ifdef(CONFIG_TEST_SYSJUMP app PRIVATE src/test_sysjump.c)

target_sources(app PRIVATE
  src/mock_switchcap.c
  src/mock_input.c
  src/mock_chipset.c
)
