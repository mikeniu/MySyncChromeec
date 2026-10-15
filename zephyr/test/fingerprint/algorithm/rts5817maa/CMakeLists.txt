# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(fingerprint_algorithm_rts5817maa)

target_sources(app PRIVATE
     src/rts5817maa.c
    "${PLATFORM_EC}/zephyr/subsys/fingerprint/alg/rts5817maa_bio_alg.c"
)

# The algorithm wrapper includes headers as <zephyr/...> which live under the
# EC repository root.
target_include_directories(app PUBLIC ${PLATFORM_EC})

# Enable private algorithm path for unit test mocking.
target_compile_definitions(app PRIVATE
    CONFIG_HAVE_RTS5817MAA_PRIVATE_ALGORITHM=1
)
