/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This file is to provide atomic_t definition */

#ifndef PLATFORM_EC_INCLUDE_ATOMIC_T_H_
#define PLATFORM_EC_INCLUDE_ATOMIC_T_H_

#ifndef CONFIG_ZEPHYR
#ifdef __cplusplus
extern "C" {
#endif
#ifdef TEST_BUILD
typedef int atomic_t;
#else
typedef long atomic_t;
#endif
typedef atomic_t atomic_val_t;
#ifdef __cplusplus
}
#endif
#else
#include <zephyr/sys/atomic.h>
#endif

#endif /* PLATFORM_EC_INCLUDE_ATOMIC_T_H_ */
