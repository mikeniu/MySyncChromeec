/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fakes.h"

#if defined(CONFIG_AP_PWRSEQ_S0IX)
DEFINE_FAKE_VALUE_FUNC(int, x86_non_dsx_mtl_s0ix_run, void *);
#endif /* CONFIG_AP_PWRSEQ_S0IX */
