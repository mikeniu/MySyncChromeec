/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __MOCK_HWTIMER_MOCK_H
#define __MOCK_HWTIMER_MOCK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mock_ctrl_hwtimer {
	uint32_t clock_val;
	uint32_t event_deadline;
	bool auto_increment;
};

#define MOCK_CTRL_DEFAULT_HWTIMER                                            \
	(struct mock_ctrl_hwtimer)                                           \
	{                                                                    \
		.clock_val = 0, .event_deadline = 0, .auto_increment = true, \
	}

extern struct mock_ctrl_hwtimer mock_ctrl_hwtimer;

#ifdef __cplusplus
}
#endif

#endif /* __MOCK_HWTIMER_MOCK_H */
