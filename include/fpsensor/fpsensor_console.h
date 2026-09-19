/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_CONSOLE_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_CONSOLE_H_

#include "console.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CPRINTF(format, args...) cprintf(CC_FP, format, ##args)
#define CPRINTS(format, args...) cprints(CC_FP, format, ##args)

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_CONSOLE_H_ */
