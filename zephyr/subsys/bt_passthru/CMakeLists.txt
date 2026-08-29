# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_include_directories(
	${PLATFORM_EC}/third_party/chre/include
)

zephyr_library_sources(
	bt_passthru_core.cc
	zephyr_bt_sender.cc
)
