# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE
	src/stubs.c
)

target_sources_ifdef(CONFIG_TEST_BOARD_BLUEY_CHIPSET app PRIVATE
	src/test_chipset.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/bluey/src/chipset.c
)

target_sources_ifdef(CONFIG_TEST_BOARD_BLUEY_FAN app PRIVATE
	src/test_fan.c
	${PLATFORM_EC_PROGRAM_DIR}/bluey/bluey/src/fan.c
)

target_compile_definitions(app PRIVATE
	CONFIG_AP_POWER_CONTROL
	CONFIG_AP_ARM_QUALCOMM_QC_EXP
	CONFIG_CHIPSET_QC_EXP
)
