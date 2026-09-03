# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.24.0)
find_package(Zephyr REQUIRED HINTS "$ENV{ZEPHYR_BASE}")
project(pw_hdlc_test)

zephyr_include_directories(
  "${PLATFORM_EC}/include"
  "${PLATFORM_EC}/test"
  "${PLATFORM_EC}/zephyr/include"
  "${PLATFORM_EC}/zephyr/shim/include"
)

target_sources(app PRIVATE src/main.cc)


target_link_libraries(app
  PRIVATE
    pw_hdlc.default_addresses
    pw_hdlc.encoder
    pw_hdlc.decoder
    pw_stream
    pw_bytes
    pw_checksum
)

zephyr_compile_definitions(PW_CHECKSUM_CRC32_DEFAULT_IMPL=4)
