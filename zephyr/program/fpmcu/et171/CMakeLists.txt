# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# SPDX-License-Identifier: Apache-2.0

set(EXPECTED_ROLLBACK_VERSION 0)

if(NOT CONFIG_PLATFORM_EC_ROLLBACK_VERSION EQUAL EXPECTED_ROLLBACK_VERSION)
	message(FATAL_ERROR "Rollback version has changed! Expected ${EXPECTED_ROLLBACK_VERSION}, but ${CONFIG_PLATFORM_EC_ROLLBACK_VERSION} is set.")
endif()

zephyr_library_sources("src/platform.c")
zephyr_library_sources("src/flash_qspi.c")

if(DEFINED CONFIG_CROS_EC_RO)

set(flash_header_output_bin ${CMAKE_BINARY_DIR}/flash_header.bin)
zephyr_file_copy(flash_header.bin ${flash_header_output_bin})

endif()
