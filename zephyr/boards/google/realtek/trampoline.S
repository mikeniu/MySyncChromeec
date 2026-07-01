/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

_ASM_FILE_PROLOGUE

GTEXT(__trampoline)

/* We place this in a special section that will be located at the start of
 * flash. We use 'trampoline' as the section name, and 'code' as subsection.
 * This expands to .trampoline.code section.
 */
SECTION_SUBSEC_FUNC(trampoline, code, __trampoline)
    /* Load the address of the actual vector table.
     * The vector table is aligned to 0x400, and is represented by the symbol
     * _vector_start.
     */
    ldr r0, =_vector_start
    /* Load the initial Stack Pointer (MSP) from the first entry of vector
     * table
     */
    ldr sp, [r0]
    /* Load the Reset Handler address from the second entry of vector table */
    ldr r1, [r0, #4]
    /* Jump to Reset Handler */
    bx r1
