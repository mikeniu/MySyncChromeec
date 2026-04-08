/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task management module for Chrome EC operating system */

#ifndef __CROS_EC_TASK_H
#define __CROS_EC_TASK_H

#include "common.h"
#include "task_id.h"

/**
 * Wait for any event.
 *
 * If one or more events are already pending, returns immediately.  Otherwise,
 * it de-schedules the calling task and wakes up the next one in the priority
 * order.  Automatically clears the bitmap of received events before returning.
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

/**
 * Reset task profiling statistics.
 */
void task_reset_stats(void);

/**
 * Prints the list of tasks.
 *
 * Uses the command output channel.  May be called from interrupt level.
 */
void task_print_list(void);

/**
 * Prints the task profiling info.
 *
 * Uses the command output channel.  May be called from interrupt level.
 */
void task_print_profiling(void);

/**
 * Update time spent in an IRQ.
 *
 * @param irq		IRQ number
 * @param duration	Duration in microseconds
 */
void task_update_irq_time(int irq, uint32_t duration);

/**
 * Pause tracking of interrupt-disabled time.
 * Used before entering sleep modes where interrupts are intentionally disabled.
 */
void task_pause_irq_disabled_timing(void);

/**
 * Resume tracking of interrupt-disabled time.
 * Used after exiting sleep modes.
 */
void task_resume_irq_disabled_timing(void);

/**
 * Returns the name of the task.
 */
const char *task_get_name(task_id_t tskid);

#endif /* __CROS_EC_TASK_H */
