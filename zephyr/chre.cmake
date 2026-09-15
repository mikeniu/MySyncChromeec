# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if(CONFIG_ISH_CHRE)
  target_sources(app PRIVATE shim/src/chre.cc)
  target_link_libraries(app PRIVATE chre)
endif()
