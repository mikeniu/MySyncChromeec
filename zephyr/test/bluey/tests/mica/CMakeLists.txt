# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE
	src/stubs.c
)

target_sources_ifdef(CONFIG_TEST_BOARD_MICA_CHIPSET app PRIVATE
	src/test_chipset.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/mica/src/chipset.c
)

target_sources_ifdef(CONFIG_TEST_BOARD_MICA_EXTPOWER app PRIVATE
	src/test_extpower.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/mica/src/extpower_custom.c
)

target_sources_ifdef(CONFIG_TEST_BOARD_MICA_FAN app PRIVATE
	src/test_fan.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/mica/src/fan.c
)

target_sources_ifdef(CONFIG_TEST_BOARD_MICA_KEYBOARD app PRIVATE
	src/test_keyboard.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/mica/src/keyboard_customization.c
)

target_sources_ifdef(CONFIG_TEST_BOARD_MICA_LED app PRIVATE
	src/test_led.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/mica/src/led.c
)

target_sources_ifdef(CONFIG_TEST_BOARD_MICA_THROTTLE app PRIVATE
	src/test_throttle.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/mica/src/throttle.c
)

target_compile_definitions(app PRIVATE
	CONFIG_AP_POWER_CONTROL
	CONFIG_AP_ARM_QUALCOMM_QC_EXP
	CONFIG_CHIPSET_QC_EXP
	CONFIG_PLATFORM_EC_EXTPOWER_CUSTOM
	CONFIG_PLATFORM_EC_BATTERY_PRESENT_CUSTOM
	CONFIG_PLATFORM_EC_TEMP_SENSOR
	CONFIG_INPUT_KBD_ACTUAL_KEY_MASK_DYNAMIC
	CONFIG_INPUT_KBD_MATRIX_THREAD_STACK_SIZE=1024
	FAN_CH_COUNT=2
)



target_include_directories(app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/bluey/mica/src
)
