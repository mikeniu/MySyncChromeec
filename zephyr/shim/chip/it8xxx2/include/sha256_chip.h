/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_CHIP_IT8XXX2_INCLUDE_SHA256_CHIP_H_
#define PLATFORM_EC_ZEPHYR_SHIM_CHIP_IT8XXX2_INCLUDE_SHA256_CHIP_H_

#include "common.h"

struct sha256_ctx {
	/* W[0] ~ W[15] */
	uint32_t w[16];
	/* reserved */
	uint32_t reserved1[8];
	/* H[0] ~ H[7] */
	uint32_t h[8];
	/* reserved */
	uint32_t reserved2[30];
	uint32_t w_index;
	uint32_t total_len;
	/* K[0] ~ K[63] */
	uint32_t k[64];
} __aligned(256);

void SHA256_abort(struct sha256_ctx *ctx);

#ifdef CONFIG_ZTEST
extern uint8_t it8xxx2_sha256_get_sha1hbaddr(void);
extern uint8_t it8xxx2_sha256_get_sha2hbaddr(void);
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_CHIP_IT8XXX2_INCLUDE_SHA256_CHIP_H_ */
