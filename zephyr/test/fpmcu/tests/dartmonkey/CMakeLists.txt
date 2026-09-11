# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories("include")
target_include_directories(app PRIVATE "include")

target_sources_ifdef(CONFIG_TEST_BOARD_POWER app PRIVATE
	src/power.c
	${PLATFORM_EC_PROGRAM_DIR}/fpmcu/${CONFIG_TEST_BOARD_NAME}/src/power.c)

target_sources_ifdef(CONFIG_TEST_BOARD_EC_QUIRKS app PRIVATE
	src/ec_quirks.c
	${PLATFORM_EC_PROGRAM_DIR}/fpmcu/${CONFIG_TEST_BOARD_NAME}/src/ec_quirks.c)

target_sources_ifdef(CONFIG_TEST_BOARD_INIT app PRIVATE
	src/init.c
	${PLATFORM_EC_PROGRAM_DIR}/fpmcu/${CONFIG_TEST_BOARD_NAME}/src/init.c)

target_sources_ifdef(CONFIG_TEST_BOARD_CLOCK app PRIVATE
	src/clock.c
	${PLATFORM_EC}/zephyr/shim/chip/stm32/clock.c)

zephyr_compile_definitions_ifdef(CONFIG_TEST_BOARD_INIT
	PINCTRL_STATE_PROD=PINCTRL_STATE_PRIV_START)

zephyr_compile_definitions_ifdef(CONFIG_TEST_BOARD_CLOCK
	CONFIG_SOC_SERIES_STM32H7X=1)
