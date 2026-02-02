/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_ASCP_H_
#define ZEPHYR_INCLUDE_ASCP_H_

#include <errno.h>
#include <stdint.h>

/** ASCP data access API. */
struct ascp_api {
	int (*get_claim)(struct ec_response_fp_ascp_claim *res);
};

/**
 * Get pointer to ASCP API instance.
 *
 * @return Pointer to the algorithm instance.
 */
static inline const struct ascp_api *ascp_api_get()
{
	extern struct ascp_api ascp_api_instance;
	return &ascp_api_instance;
}

#endif /* ZEPHYR_INCLUDE_ASCP_H_ */
