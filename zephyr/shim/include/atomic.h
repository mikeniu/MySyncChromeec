/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_ATOMIC_H_
#define ZEPHYR_SHIM_INCLUDE_ATOMIC_H_

#include <zephyr/sys/atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline atomic_val_t atomic_clear_bits(atomic_t *addr, atomic_val_t bits)
{
	return atomic_and(addr, ~bits);
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SHIM_INCLUDE_ATOMIC_H_ */
