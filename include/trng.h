/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_INCLUDE_TRNG_H
#define __EC_INCLUDE_TRNG_H

#include <stddef.h>
#include <stdint.h>

#include <common.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Zephyr driver is responsible for initializing, enabling and disabling
 * hardware. In this case, trng_init() and trng_exit() does nothing.
 */
#define trng_init()
#define trng_exit()

/**
 * Output len random bytes into buffer.
 *
 * Not supported on all platforms.
 **/
void trng_rand_bytes(void *buffer, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* __EC_INCLUDE_TRNG_H */
