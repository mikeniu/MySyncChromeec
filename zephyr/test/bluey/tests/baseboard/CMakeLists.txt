# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE
	src/stubs.c
	src/test_battery.c
	src/test_switchcap.c
	src/test_chipset.c
	src/test_throttle.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/src/battery.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/src/switchcap.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/src/chipset.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/src/throttle.c
)

target_compile_definitions(app PRIVATE
	CONFIG_AP_POWER_CONTROL
	HAS_TASK_CHIPSET
	CONFIG_PLATFORM_EC_BATTERY_PRESENT_CUSTOM
	CONFIG_PLATFORM_EC_BATTERY_ACCESS_LIMIT
	CONFIG_PLATFORM_EC_PMIC_PASSTHRU_POWER_SIGNALS
	CONFIG_PLATFORM_EC_POWERSEQ_CPU_PROCHOT_ACTIVE_LOW
	CONFIG_PLATFORM_EC_THROTTLE_AP
)
