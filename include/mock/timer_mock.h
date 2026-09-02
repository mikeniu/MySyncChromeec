/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef INCLUDE_MOCK_TIMER_MOCK_H_
#define INCLUDE_MOCK_TIMER_MOCK_H_

#include "timer.h"

#ifdef __cplusplus
extern "C" {
#endif

void set_time(timestamp_t now_);

timestamp_t get_time(void);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_MOCK_TIMER_MOCK_H_ */
