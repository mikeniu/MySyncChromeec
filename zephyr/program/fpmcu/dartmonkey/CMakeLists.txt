# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources("src/power.c")
zephyr_library_sources("src/init.c")

zephyr_library_sources_ifdef(CONFIG_DARTMONKEY_EC_RO_QUIRKS "src/ec_quirks.c")
