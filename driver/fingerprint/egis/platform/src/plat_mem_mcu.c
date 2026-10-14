/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "plat_log.h"
#include "plat_mem.h"

#include <stdlib.h>

void *sys_alloc(size_t count, size_t size)
{
	void *addr = malloc(size);

	if (addr == NULL) {
		CPRINTS("Error - %s of size %u failed.", __func__, size);
		return NULL;
	}
	return addr;
}

void sys_free(void *ptr)
{
	free(ptr);
}
