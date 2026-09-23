/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_DEBUG_H_
#define PLATFORM_EC_INCLUDE_DEBUG_H_

#include "common.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Indicates if a debugger is actively connected.
 */
__override_proto bool debugger_is_connected(void);

/*
 * This function looks for signs that a debugger was attached. If we
 * see that a debugger was attached, we know that the chip's security features
 * may function as if the debugger is still attached.
 *
 * This should be true while a debugger is actively connected, too.
 */
__override_proto bool debugger_was_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_DEBUG_H_ */
