/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_ASCP_H_
#define ZEPHYR_INCLUDE_ASCP_H_

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
int get_ascp_claim(struct ec_response_fp_ascp_claim *res);

/**
 * Get ASCP secret ephemeral key.
 *
 * @return Pointer to the secret ephemeral key.
 */
const uint8_t *get_ascp_sk_f(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ASCP_H_ */
