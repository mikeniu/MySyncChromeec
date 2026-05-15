# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories("${ZEPHYR_BASE}/arch/arm/include")

zephyr_library_sources_ifdef(CONFIG_TEST debug.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_IRQ irq_command.c)
if (CONFIG_MPU)
  # Cortex-M33 also selects CONFIG_ARMV7_M_ARMV8_M_MAINLINE, so check v8-M first.
  if (CONFIG_ARMV8_M_BASELINE OR CONFIG_ARMV8_M_MAINLINE)
    zephyr_library_sources(mpu_v8m.c)
  else()
    zephyr_library_sources(mpu_v7m.c)
  endif()
endif()
zephyr_library_sources(panic.c)
