# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library()
zephyr_library_include_directories(${PLATFORM_EC}/zephyr/include)
zephyr_library_sources(src/hdlc_router.cc)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HDLC_ROUTER_SHELL src/hdlc_shell_server.cc)
zephyr_library_link_libraries(
	pw_bytes
	pw_checksum
	pw_hdlc.default_addresses
	pw_hdlc.encoder
	pw_hdlc.decoder
	pw_result
	pw_status
	pw_stream
)
