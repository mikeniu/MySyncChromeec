# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

add_subdirectory(handlers)

zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ_S0IX_ERROR_RECOVERY
	x86_non_dsx_common_pwrseq_host_sleep.c)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ_CONSOLE
	x86_non_dsx_common_pwrseq_console.c)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ_HOST_CMD
	x86_non_dsx_common_pwrseq_host_command.c)
