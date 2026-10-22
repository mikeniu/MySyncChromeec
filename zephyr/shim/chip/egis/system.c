/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"

#include <zephyr/arch/riscv/pmp.h>

#ifdef CONFIG_RISCV_PMP
void chip_pre_system_jump(void)
{
	/*
	 * Clear all Physical Memory Protection (PMP) entries before jumping to
	 * the new image. When CONFIG_HW_STACK_PROTECTION is enabled, the
	 * current image's PMP configuration for stack boundaries can interfere
	 * with the stack setup of the new image, leading to crashes. Clearing
	 * ensures the new image starts with a clean PMP state.
	 */
	z_riscv_pmp_clear_all();
}
#endif

/* For the Egis chips, a jump instruction is placed at the beginning of firmware
 * image. Refer to start.S file for more details.
 */
uintptr_t system_get_fw_reset_vector(uintptr_t base)
{
	return base;
}
