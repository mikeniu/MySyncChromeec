/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SUBSYS_FINGERPRINT_ALG_FPC_BEP_BIO_ALG_H_
#define PLATFORM_EC_ZEPHYR_SUBSYS_FINGERPRINT_ALG_FPC_BEP_BIO_ALG_H_

#include <stdint.h>

#include <driver/fingerprint/fpc/bep/fpc_bio_algorithm.h>

/*
 * Constant value for the enrollment data size
 *
 * Size of private fp_bio_enrollment_t
 */
#define FP_ALGORITHM_ENROLLMENT_SIZE (4)

/* Declaration of FPC1025 algorithm. */
extern const fpc_bep_algorithm_t fpc_bep_algorithm_pfe_1025;

#endif /* PLATFORM_EC_ZEPHYR_SUBSYS_FINGERPRINT_ALG_FPC_BEP_BIO_ALG_H_ */
