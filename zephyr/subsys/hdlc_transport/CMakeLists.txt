# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library()

zephyr_library_sources(
    src/hdlc_transport.cc
)

zephyr_library_sources_ifdef(
    CONFIG_SHELL_BACKEND_HDLC
    src/shell_hdlc.c
)

zephyr_library_sources_ifdef(
    CONFIG_LOG_BACKEND_HDLC
    src/log_backend_hdlc.c
)

zephyr_include_directories(include)

zephyr_library_link_libraries(
    pw_hdlc.encoder
    pw_hdlc.decoder
    pw_hdlc.default_addresses
    pw_stream
)
