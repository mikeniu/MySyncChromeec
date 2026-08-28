# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE
	src/stubs.c
	src/test_chipset.c
	src/test_extpower.c
	src/test_fan.c
	src/test_keyboard.c
	src/test_led.c
	src/test_throttle.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/mica/src/chipset.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/mica/src/extpower_custom.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/mica/src/fan.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/mica/src/led.c
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
