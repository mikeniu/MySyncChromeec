# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories("${PLATFORM_EC}/zephyr/")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MUTEX_HISTORY mutex_history.c)
zephyr_library_sources_ifdef(CONFIG_TRACING_USER tracing.c)

