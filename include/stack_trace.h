/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trace dump module */

#ifndef PLATFORM_EC_INCLUDE_STACK_TRACE_H_
#define PLATFORM_EC_INCLUDE_STACK_TRACE_H_

#ifdef EMU_BUILD
/*
 * Register trace dump handler for emulator. Trace dump is printed to stderr
 * when SIGUSR2 is received.
 */
void task_register_tracedump(void);

/* Dump current stack trace */
void task_dump_trace(void);
#else
static inline void task_register_tracedump(void)
{
}
static inline void task_dump_trace(void)
{
}
#endif

#endif /* PLATFORM_EC_INCLUDE_STACK_TRACE_H_ */
