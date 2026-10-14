/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

#ifndef PLATFORM_EC_INCLUDE_TASK_H_
#define PLATFORM_EC_INCLUDE_TASK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "atomic_t.h"
#include "common.h"
#include "compile_time_macros.h"
#include "task_id.h"

#include <stdbool.h>

/* Task event bitmasks */
/* Tasks may use the bits in TASK_EVENT_CUSTOM_BIT for their own events */
#define TASK_EVENT_CUSTOM_BIT(x) BUILD_CHECK_INLINE(BIT(x), BIT(x) & 0x0ffff)

#define TASK_EVENT_PD_AWAKE BIT(18)

/* For test-only builds, may be used by some tasks to restart themselves. */
#define TASK_EVENT_RESET_DONE BIT(28)
/* task_wake() called on task */
#define TASK_EVENT_WAKE BIT(29)
/*
 * Timer expired.  For example, task_wait_event() timed out before receiving
 * another event.
 */
#define TASK_EVENT_TIMER (1U << 31)

/* Maximum time for task_wait_event() */
#define TASK_MAX_WAIT_US 0x7fffffff

/**
 * Set a task event.
 *
 * If the task is higher priority than the current task, this will cause an
 * immediate context switch to the new task.
 *
 * Can be called both in interrupt context and task context.
 *
 * @param tskid		Task to set event for
 * @param event		Event bitmap to set (TASK_EVENT_*)
 */
void task_set_event(task_id_t tskid, uint32_t event);

/**
 * Wake a task.  This sends it the TASK_EVENT_WAKE event.
 *
 * @param tskid		Task to wake
 */
static inline void task_wake(task_id_t tskid)
{
	task_set_event(tskid, TASK_EVENT_WAKE);
}

/**
 * Return the identifier of the task currently running.
 */
task_id_t task_get_current(void);

/**
 * Return a pointer to the bitmap of events of the task.
 */
atomic_t *task_get_event_bitmap(task_id_t tskid);

/**
 * Wait for the next event.
 *
 * If one or more events are already pending, returns immediately.  Otherwise,
 * it de-schedules the calling task and wakes up the next one in the priority
 * order.  Automatically clears the bitmap of received events before returning
 * the events which are set.
 *
 * @param timeout_us	If > 0, sets a timer to produce the TASK_EVENT_TIMER
 *			event after the specified micro-second duration.
 *
 * @return The bitmap of received events.
 */
uint32_t task_wait_event(int timeout_us);

/**
 * Wait for any event included in an event mask.
 *
 * If one or more events are already pending, returns immediately.  Otherwise,
 * it de-schedules the calling task and wakes up the next one in the priority
 * order.  Automatically clears the bitmap of received events before returning
 * the events which are set.
 *
 * @param event_mask	Bitmap of task events to wait for.
 *
 * @param timeout_us	If > 0, sets a timer to produce the TASK_EVENT_TIMER
 *			event after the specified micro-second duration.
 *
 * @return		The bitmap of received events. Includes
 *			TASK_EVENT_TIMER if the timeout is reached.
 */
uint32_t task_wait_event_mask(uint32_t event_mask, int timeout_us);

typedef struct k_mutex mutex_t;

#define mutex_lock(mtx) (k_mutex_lock(mtx, K_FOREVER))
#define mutex_unlock(mtx) (k_mutex_unlock(mtx))

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_TASK_H_ */
