/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "vboot.h"

#define CPRINTS(format, args...) cprints(CC_VBOOT, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_VBOOT, format, ##args)

int vboot_is_padding_valid(const uint8_t *data, uint32_t start, uint32_t end)
{
	const uint32_t *data32 = (const uint32_t *)data;
	int i;

	if (start > end)
		return EC_ERROR_INVAL;

	if (start % 4 || end % 4)
		return EC_ERROR_INVAL;

	for (i = start / 4; i < end / 4; i++) {
		if (data32[i] != 0xffffffff)
			return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}
