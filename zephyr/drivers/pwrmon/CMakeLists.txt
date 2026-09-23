# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_include_directories(include)
zephyr_library_sources_ifdef(CONFIG_PWRMON pwrmon.c)
zephyr_library_sources_ifdef(CONFIG_PWRMON_POWER_MONITOR_COMMON power_monitor_common.c)
zephyr_library_sources_ifdef(CONFIG_PWRMON_POWER_MONITOR_PAC194X power_monitor_pac194x.c)
