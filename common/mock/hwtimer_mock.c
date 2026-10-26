/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hwtimer.h"
#include "mock/hwtimer_mock.h"

#if !defined(TEST_BUILD) || !defined(BOARD_HOST)
#error "Mocks should only be in the host test build."
#endif

struct mock_ctrl_hwtimer mock_ctrl_hwtimer = MOCK_CTRL_DEFAULT_HWTIMER;

void __hw_clock_event_set(uint32_t deadline)
{
	mock_ctrl_hwtimer.event_deadline = deadline;
}

uint32_t __hw_clock_event_get(void)
{
	return mock_ctrl_hwtimer.event_deadline;
}

void __hw_clock_event_clear(void)
{
	mock_ctrl_hwtimer.event_deadline = 0;
}

uint32_t __hw_clock_source_read(void)
{
	if (mock_ctrl_hwtimer.auto_increment)
		++mock_ctrl_hwtimer.clock_val;
	return mock_ctrl_hwtimer.clock_val;
}

void __hw_clock_source_set(uint32_t ts)
{
	mock_ctrl_hwtimer.clock_val = ts;
}

int __hw_clock_source_init(uint32_t start_t)
{
	mock_ctrl_hwtimer.clock_val = start_t;
	return 0;
}
