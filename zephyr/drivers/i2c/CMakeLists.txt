# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

dt_chosen(HC_I2C_NODE PROPERTY hc_i2c)

if(DEFINED HC_I2C_NODE AND DEFINED CONFIG_PLATFORM_EC_SYSTEM_UNLOCKED)
     # The chosen node exists, enable related sources
     zephyr_library_sources(i2c_scan.c)
endif()
