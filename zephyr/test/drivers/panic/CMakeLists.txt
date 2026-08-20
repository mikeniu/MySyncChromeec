# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE
  src/panic.c
)

add_subdirectory(
    ${PLATFORM_EC}/zephyr/test/drivers/mock_panic_output
    mock_panic_output
)
