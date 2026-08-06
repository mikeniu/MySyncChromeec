# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE src/stubs.c)

target_sources_ifdef(CONFIG_PDC_RUNTIME_PORT_CONFIG app PRIVATE
	src/atriarvp_rtk_test.c
	${PLATFORM_EC}/zephyr/program/atria/atriarvp-rtk/src/rvp_usbc_runtime.c
)
