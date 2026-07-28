# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources(common_pwrseq_sm_handler.c)
zephyr_library_sources(chipset_power_state.c)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ_ADL
	adlp_pwrseq_sm.c)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ_MTL
	mtl_pwrseq_sm.c)
