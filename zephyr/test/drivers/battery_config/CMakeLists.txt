# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources_ifdef(CONFIG_LINK_TEST_SUITE_BATTERY_CONFIG
		app PRIVATE src/battery_config.c)
