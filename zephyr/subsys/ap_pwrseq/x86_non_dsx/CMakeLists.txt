# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if (DEFINED CONFIG_AP_PWRSEQ_DRIVER)
add_subdirectory(drv_handlers)
else ()
add_subdirectory(handlers)
endif ()

zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ
	x86_non_dsx_common.c)
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ_S0IX_ERROR_RECOVERY
	x86_non_dsx_common_pwrseq_host_sleep.c)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ_CONSOLE
	x86_non_dsx_common_pwrseq_console.c)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ_HOST_CMD
	x86_non_dsx_common_pwrseq_host_command.c)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ_ADL
	x86_non_dsx_adlp_pwrseq_sm.c)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ_MTL
	x86_non_dsx_mtl_pwrseq_sm.c)
