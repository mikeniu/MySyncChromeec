# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources_ifdef(CONFIG_TEST_BOARD_PENGHU_KEYBOARD app PRIVATE src/keyboard.c)
target_sources_ifdef(CONFIG_TEST_BOARD_PENGHU_FAN app PRIVATE src/fan.c)
