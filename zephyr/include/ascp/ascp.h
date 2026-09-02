/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_ASCP_ASCP_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_ASCP_ASCP_H_

#include "ec_commands.h"

#include <errno.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get ASCP claim data.
 *
 * @param[out] res Pointer to the response structure.
 * @return EC_SUCCESS if successful, EC_ERROR_* otherwise.
 */
int ascp_get_claim(struct ec_response_fp_ascp_claim *res);

/**
 * Get ASCP secret ephemeral key.
 *
 * @param[out] buffer Buffer to store the secret ephemeral key. Must be at least
 *   32 bytes in size.
 * @param[in] len Length of the buffer.
 * @return EC_SUCCESS if successful, EC_ERROR_* otherwise.
 */
int ascp_get_sk_f(uint8_t *buffer, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_ASCP_ASCP_H_ */
