/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_SHA256_HW_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_SHA256_HW_H_

#include <zephyr/crypto/crypto.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sha256_ctx {
	/*
	 * This is used to buffer:
	 *   1. the result (digest) of the SHA256 computation.
	 *   2. the 1st block input data (the key padding) for hmac_SHA256_step.
	 */
	uint8_t buf[SHA256_BLOCK_SIZE];
	struct hash_ctx hash_sha256;
} __aligned(4);

void SHA256_abort(struct sha256_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_SHA256_HW_H_ */
