/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helpers for the boringssl elliptic curve key interface. */

#ifndef PLATFORM_EC_INCLUDE_CRYPTO_ELLIPTIC_CURVE_KEY_H_
#define PLATFORM_EC_INCLUDE_CRYPTO_ELLIPTIC_CURVE_KEY_H_

#include "openssl/ec_key.h"
#include "openssl/mem.h"

/**
 * Generate a p256 ECC key.
 * @return key on success, nullptr on failure
 */
bssl::UniquePtr<EC_KEY> generate_elliptic_curve_key();

#endif /* PLATFORM_EC_INCLUDE_CRYPTO_ELLIPTIC_CURVE_KEY_H_ */
