# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function(kernel_lto_allow_list out_var)
  set(extra timer.c)
  set(${out_var} "${extra}" PARENT_SCOPE)
endfunction()
