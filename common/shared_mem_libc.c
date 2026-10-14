/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @brief Shared mem implementation that uses malloc/free from libc.
 */

#include "common.h"
#include "console.h"
#include "link_defs.h"
#include "shared_mem.h"
#include "system.h"

#include <stdlib.h>

#include <zephyr/kernel.h>

#include <malloc.h>

int shared_mem_size(void)
{
	return system_usable_ram_end() - (uintptr_t)__shared_mem_buf;
}

int shared_mem_acquire(int size, char **dest_ptr)
{
	*dest_ptr = NULL;

	if (k_is_in_isr())
		return EC_ERROR_INVAL;

	*dest_ptr = malloc(size);
	if (!*dest_ptr)
		return EC_ERROR_BUSY;

	return EC_SUCCESS;
}

void shared_mem_release(void *ptr)
{
	if (k_is_in_isr())
		return;

	free(ptr);
}

#ifdef CONFIG_CMD_SHMEM
static int command_shmem(int argc, const char **argv)
{
	/* This function is deprecated in glibc, in preference for mallinfo2,
	 * since mallinfo2 provides information as a series of `size_t`s instead
	 * of `int`s.
	 *
	 * We needn't worry about reporting for GB of RAM, so ignore the
	 * deprecation warning here.
	 */
	DISABLE_COMPILER_WARNING("-Wdeprecated-declarations")
	struct mallinfo info = mallinfo();
	ENABLE_COMPILER_WARNING("-Wdeprecated-declarations")

	/* The max size of shared mem region, for a given image. */
	ccprintf("System Total:     %d\n", shared_mem_size());
	/* The total size currently reserved from system for malloc. */
	ccprintf("Malloc Reserved:  %d\n", info.arena);
	/* The total allocated space, of the reserved space. */
	ccprintf("Malloc Allocated: %d\n", info.uordblks);
	/* The total unused space, of the reserved space. */
	ccprintf("Malloc Free:      %d\n", info.fordblks + info.fsmblks);
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(shmem, command_shmem, NULL,
			     "Print shared memory stats");
#endif /* CONFIG_CMD_SHMEM */
