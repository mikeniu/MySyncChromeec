# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if(CONFIG_REALTEK_RTS5912_TRAMPOLINE)
  zephyr_library()
  zephyr_library_sources(trampoline.S)
  zephyr_linker_sources(ROM_START SORT_KEY 0x0_trampoline trampoline.ld)
endif()
