# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/brox/juchi/include")
zephyr_include_directories("${PLATFORM_EC}/zephyr/shim/chip/it8xxx2/include")

target_sources_ifdef(CONFIG_TEST_BOARD_JUCHI_KEYBOARD app PRIVATE src/keyboard.c)
target_sources_ifdef(CONFIG_TEST_BOARD_JUCHI_KEYBOARD app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/brox/juchi/src/keyboard.c
	${PLATFORM_EC_PROGRAM_DIR}/brox/juchi/src/keyboard_customization.c)

target_sources_ifdef(CONFIG_TEST_BOARD_JUCHI_FAN app PRIVATE src/fan.c)
target_sources_ifdef(CONFIG_TEST_BOARD_JUCHI_FAN app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/brox/juchi/src/thermal.c)
