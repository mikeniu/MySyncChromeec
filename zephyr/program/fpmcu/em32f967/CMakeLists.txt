# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(EXPECTED_ROLLBACK_VERSION 0)

if(NOT CONFIG_PLATFORM_EC_ROLLBACK_VERSION EQUAL EXPECTED_ROLLBACK_VERSION)
	message(FATAL_ERROR "Rollback version has changed! Expected ${EXPECTED_ROLLBACK_VERSION}, but ${CONFIG_PLATFORM_EC_ROLLBACK_VERSION} is set.")
endif()

zephyr_library_sources("src/platform.c")
