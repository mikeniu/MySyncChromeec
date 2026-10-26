/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hwtimer.h"

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

static uint32_t clock_val;
static uint32_t event_deadline;

void __hw_clock_event_set(uint32_t deadline)
{
	event_deadline = deadline;
}

uint32_t __hw_clock_event_get(void)
{
	return event_deadline;
}

void __hw_clock_event_clear(void)
{
	event_deadline = 0;
}

uint32_t __hw_clock_source_read(void)
{
	return ++clock_val;
}

void __hw_clock_source_set(uint32_t ts)
{
	clock_val = ts;
}

int __hw_clock_source_init(uint32_t start_t)
{
	clock_val = start_t;
	return 0;
}
