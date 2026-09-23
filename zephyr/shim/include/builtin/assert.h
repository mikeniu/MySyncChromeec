/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_BUILTIN_ASSERT_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_BUILTIN_ASSERT_H_

#include <zephyr/sys/__assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#undef ASSERT
#undef assert
#define ASSERT __ASSERT_NO_MSG
#define assert __ASSERT_NO_MSG

/* TODO(b/269175417): This should be handled in Zephyr __assert.h */
#ifndef __ASSERT_UNREACHABLE
#define __ASSERT_UNREACHABLE CODE_UNREACHABLE
#endif

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_BUILTIN_ASSERT_H_ */
