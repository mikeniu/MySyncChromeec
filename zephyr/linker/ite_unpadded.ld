/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _ITE_UNPADDED_LD_
#define _ITE_UNPADDED_LD_

#include <zephyr/linker/linker-tool.h>

.rom_unpadded (NOLOAD) :
{
	/* Calculate unpadded flash footprint */
	. += ABSOLUTE(__ilm_flash_start) - ABSOLUTE(__rom_region_start);
} > ROM_UNPADDED

.ram_unpadded (NOLOAD) :
{
	/* Calculate unpadded RAM footprint (ILM + BSS/Data + the h2ram pool itself) */
#ifdef CONFIG_ESPI_IT8XXX2
	. += (ABSOLUTE(__data_region_end) - CONFIG_SRAM_BASE_ADDRESS) + ABSOLUTE(_h2ram_pool_size);
#else
	. += (ABSOLUTE(__data_region_end) - CONFIG_SRAM_BASE_ADDRESS);
#endif
} > RAM_UNPADDED

#endif /* _ITE_UNPADDED_LD_ */