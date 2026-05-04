/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_THREADINFO_H_
#define ZEPHYR_SHIM_INCLUDE_THREADINFO_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#define THREADINFO_MAX_COL_WIDTH 16

/* Width of the Name column. */
#ifdef CONFIG_THREAD_NAME
#define COL_WIDTH_NAME                                           \
	(CONFIG_THREAD_MAX_NAME_LEN < THREADINFO_MAX_COL_WIDTH ? \
		 CONFIG_THREAD_MAX_NAME_LEN :                    \
		 THREADINFO_MAX_COL_WIDTH)
#else
#define COL_WIDTH_NAME 0
#endif

#define THREADINFO_COL_SEP_PRE "| "
#define THREADINFO_COL_SEP_POST " "

/*
 * Defines the columns displayed by the threadinfo command.
 * This uses the X-macro pattern.
 */
#define THREADINFO_COLUMNS_FOREACH(X)                                        \
	X(NAME, COL_WIDTH_NAME, "Name", fill_name,                           \
	  IS_ENABLED(CONFIG_THREAD_NAME))                                    \
	X(ID, 8, "tid", fill_id, true)                                       \
	X(ENTRY, 8, "Entry", fill_entry, true)                               \
	X(PRIO, 4, "Prio", fill_prio, true)                                  \
	X(STATE, 5, "State", fill_state, true)                               \
	X(WAITOBJ, 8, "WaitObj", fill_waitobj, true)                         \
	X(TIMEOUT, 7, "Timeout", fill_timeout, true)                         \
	X(TIME, 7, "Time", fill_time, IS_ENABLED(CONFIG_SCHED_THREAD_USAGE)) \
	X(PEAK, 7, "Peak", fill_peak,                                        \
	  IS_ENABLED(CONFIG_SCHED_THREAD_USAGE_ANALYSIS))                    \
	X(AVG, 7, "Avg", fill_avg,                                           \
	  IS_ENABLED(CONFIG_SCHED_THREAD_USAGE_ANALYSIS))                    \
	X(STKCUR, 6, "StkCur", fill_stkcur,                                  \
	  IS_ENABLED(CONFIG_THREAD_STACK_INFO))                              \
	X(STKMAX, 6, "StkMax", fill_stkmax,                                  \
	  IS_ENABLED(CONFIG_THREAD_STACK_INFO))                              \
	X(STKSIZE, 6, "StkSize", fill_stksize,                               \
	  IS_ENABLED(CONFIG_THREAD_STACK_INFO))                              \
	X(PC, 8, "PC", fill_pc, true)                                        \
	X(LR, 8, "LR", fill_lr, IS_ENABLED(CONFIG_ARM))

#define THREADINFO_ENUM_VAL(col_id, width, header, func, cond) \
	THREADINFO_COL_##col_id,
enum { THREADINFO_COLUMNS_FOREACH(THREADINFO_ENUM_VAL) };
#undef THREADINFO_ENUM_VAL

/* Public functions */
void threadinfo_dump(uint32_t mask, const struct shell *shell);

#endif /* ZEPHYR_SHIM_INCLUDE_THREADINFO_H_ */
