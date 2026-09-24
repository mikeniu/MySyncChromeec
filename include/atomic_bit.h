/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_ATOMIC_BIT_H_
#define PLATFORM_EC_INCLUDE_ATOMIC_BIT_H_

#ifndef CONFIG_ZEPHYR
#include "atomic.h"

#ifdef __cplusplus
extern "C" {
#endif
static inline atomic_val_t atomic_get(const atomic_t *target)
{
	return __atomic_load_n(target, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_set(atomic_t *target, atomic_val_t value)
{
	return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}
#ifdef __cplusplus
}
#endif

#include "third_party/zephyr/atomic.h"
#endif /* CONFIG_ZEPHYR */
#endif /* PLATFORM_EC_INCLUDE_ATOMIC_BIT_H_ */
