/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if defined(CONFIG_CROS_EC_RODATA_IN_SPI_FLASH)
    __flash_rodata_lma = ALIGN(__rom_region_start + _flash_used, 4);
    __code_ram_end = __flash_rodata_lma;
    __flash_rodata_offset = __flash_rodata_lma - __rom_region_start;
    __flash_rodata_vma = CONFIG_PLATFORM_EC_MAPPED_STORAGE_BASE +
                         DT_PROP(DT_PATH(binman, ec_rw), offset) +
                         __flash_rodata_offset;

    __saved_dot = .;
    .flash_rodata __flash_rodata_vma : AT(__flash_rodata_lma)
    {
        __flash_rodata_start = .;
        *(.flash_rodata)
        *(".flash_rodata.*")
        *(.rodata.str1.*)
        *(".rodata.*.str1.*")
        . = ALIGN(4);
        __flash_rodata_end = .;
    }
    . = __saved_dot;
#endif
