# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(
  CONFIG_PDC_RUNTIME_PORT_CONFIG "src/rvp_usbc_runtime.c")

# TODO(b/384559486): Remove this once ARMv8-M libstdc++ tags are fixed.
zephyr_link_libraries("-Wl,--no-warn-mismatch")
