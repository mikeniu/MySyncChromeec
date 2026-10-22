# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add source files
target_sources(app PRIVATE src/ppc.c)
zephyr_link_libraries(-Wl,--wrap=pd_is_connected)
