# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources(x86_non_dsx_common_pwrseq_sm_handler.c)
zephyr_library_sources(x86_non_dsx_chipset_power_state.c)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ_ADL
	x86_non_dsx_adlp_pwrseq_sm.c)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ_MTL
	x86_non_dsx_mtl_pwrseq_sm.c)
