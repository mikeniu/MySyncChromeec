# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(EXPECTED_ROLLBACK_VERSION 1)

if(NOT CONFIG_PLATFORM_EC_ROLLBACK_VERSION EQUAL EXPECTED_ROLLBACK_VERSION)
	message(FATAL_ERROR "Rollback version has changed! Expected ${EXPECTED_ROLLBACK_VERSION}, but ${CONFIG_PLATFORM_EC_ROLLBACK_VERSION} is set.")
endif()

zephyr_library_sources("src/power.c")

zephyr_library_sources_ifdef(CONFIG_BLOONCHIPPER_EC_RO_QUIRKS "src/ec_quirks.c")
zephyr_library_sources_ifdef(CONFIG_EC_HOST_CMD "src/fp_transport.c")
