# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories(include)

target_sources(app PRIVATE
	src/mock_cbi.c
	src/ufsc_kb_backlight.c
	src/ufsc_thermal_fan.c
	src/ufsc_tablet_mode.c
)
