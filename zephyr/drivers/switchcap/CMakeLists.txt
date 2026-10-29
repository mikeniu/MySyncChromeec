# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_CROS_SWITCHCAP_GPIO switchcap_gpio.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SWITCHCAP_COMPARATOR
	switchcap_comparator.c)
