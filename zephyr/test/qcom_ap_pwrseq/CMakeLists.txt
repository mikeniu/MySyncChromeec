# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(qcom_ap_pwrseq)

zephyr_include_directories(
  "${PLATFORM_EC}/zephyr/shim/include"
  "${PLATFORM_EC}/test"
  "${PLATFORM_EC}"
  "${PLATFORM_EC}/include"
  "${PLATFORM_EC}/zephyr/test/test_utils"
)

zephyr_compile_definitions(
  CHROMIUM_EC
  CONFIG_ZEPHYR
  CONFIG_AP_POWER_CONTROL
  CONFIG_PLATFORM_EC_PRESERVED_END_OF_RAM_SIZE=1024
)

target_sources(app PRIVATE src/main.c src/mock_switchcap.c src/mock_input.c src/mock_chipset.c)
