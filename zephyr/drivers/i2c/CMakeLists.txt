# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_I2C_DTS_PASSTHRU i2c_dts_passthru.c)
