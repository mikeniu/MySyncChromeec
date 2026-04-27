# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories(src)

target_sources_ifdef(CONFIG_TEST_PROJECT_BATTERY app PRIVATE src/battery.c)
target_sources_ifdef(CONFIG_TEST_PROJECT_BATTERY app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/fatcat/lapis/src/battery.c)

# Make battery.h only declare battery_is_present() (the custom
# implementation comes from lapis battery.c). Define the PLATFORM_EC_
# prefixed symbol: config_chip.h #undefs CONFIG_BATTERY_PRESENT_CUSTOM
# and re-defines it only when the PLATFORM_EC_ version is set. This
# avoids pulling in the whole battery stack (which requires a
# battery-smart devicetree node).
target_compile_definitions_ifdef(CONFIG_TEST_PROJECT_BATTERY app
	PRIVATE CONFIG_PLATFORM_EC_BATTERY_PRESENT_CUSTOM)
