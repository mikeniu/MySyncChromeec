# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(hdlc_transport_test)

target_sources(app PRIVATE src/hdlc_transport_test.cc)

target_link_libraries(app PRIVATE
    pw_hdlc.encoder
    pw_hdlc.decoder
    pw_hdlc.default_addresses
    pw_stream
)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
