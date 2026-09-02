/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_MOCK_FPSENSOR_STATE_MOCK_H_
#define PLATFORM_EC_INCLUDE_MOCK_FPSENSOR_STATE_MOCK_H_

#include "ec_commands.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t default_fake_tpm_seed[FP_CONTEXT_TPM_BYTES];
extern const uint8_t
	default_fake_fp_positive_match_salt[FP_POSITIVE_MATCH_SALT_BYTES];
extern const uint8_t
	trivial_fp_positive_match_salt[FP_POSITIVE_MATCH_SALT_BYTES];

int fpsensor_state_mock_set_tpm_seed(
	const uint8_t tpm_seed[FP_CONTEXT_TPM_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_MOCK_FPSENSOR_STATE_MOCK_H_ */
