# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(atria)

# Include FFF fakes
add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

target_sources(app PRIVATE src/stubs.c)

if (DEFINED CONFIG_TEST_ATRIA_RVP_USBC_RUNTIME_3PORT)
	# Production source compiled normally; overlay provides all three PDCs.
	target_sources(app PRIVATE
		${PLATFORM_EC}/zephyr/program/atria/src/rvp_usbc_runtime.c
		src/rvp_usbc_runtime_3port_test.c)
endif()

if (DEFINED CONFIG_TEST_ATRIA_RVP_USBC_RUNTIME_2PORT)
	# Production source compiled normally; overlay provides C0 + C1 only.
	target_sources(app PRIVATE
		${PLATFORM_EC}/zephyr/program/atria/src/rvp_usbc_runtime.c
		src/rvp_usbc_runtime_2port_test.c)
endif()

if (DEFINED CONFIG_TEST_ATRIA_RVP_USBC_RUNTIME_PARTIAL)
	# Partial hardware configs: emul_fake_absent_pdc simulates absent chips
	# (compiled automatically via CONFIG_EMUL_FAKE_ABSENT_PDC in zephyr/emul/).
	target_sources(app PRIVATE
		${PLATFORM_EC}/zephyr/program/atria/src/rvp_usbc_runtime.c
		src/rvp_usbc_runtime_partial_test.c)
endif()
