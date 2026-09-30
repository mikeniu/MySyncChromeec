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

/* Used to signal that sysjump preparation has completed */
#define TASK_EVENT_SYSJUMP_READY BIT(16)

/* Used to signal that IPC layer is available for sending new data */
#define TASK_EVENT_IPC_READY BIT(17)

#define TASK_EVENT_PD_AWAKE BIT(18)

/* npcx peci event */
#define TASK_EVENT_PECI_DONE BIT(19)

/* I2C tx/rx interrupt handler completion event. */
#ifdef CHIP_STM32
#define TASK_EVENT_I2C_COMPLETION(port) (1 << ((port) + 20))
#define TASK_EVENT_I2C_IDLE (TASK_EVENT_I2C_COMPLETION(0))
#define TASK_EVENT_MAX_I2C 6
#ifdef I2C_PORT_COUNT
#if (I2C_PORT_COUNT > TASK_EVENT_MAX_I2C)
#error "Too many i2c ports for i2c events"
#endif
#endif
#else
#define TASK_EVENT_I2C_IDLE BIT(20)
#define TASK_EVENT_PS2_DONE BIT(21)
#endif

/* DMA transmit complete event */
#define TASK_EVENT_DMA_TC BIT(26)
/* ADC interrupt handler event */
#define TASK_EVENT_ADC_DONE BIT(27)
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
 * Disable CPU interrupt bit.
 *
 * This might break the system so think really hard before using these. There
 * are usually better ways of accomplishing this.
 */
void interrupt_disable(void);

/**
 * Enable CPU interrupt bit.
 */
void interrupt_enable(void);

/**
 * Check if interrupts are enabled
 */
bool is_interrupt_enabled(void);

/**
 * Return true if we are in interrupt context.
 */
bool in_interrupt_context(void);

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
 * Check if this current task is running in deferred context
 */
bool in_deferred_context(void);

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

/**
 * Return non-zero if task_start() has been called and task scheduling has
 * started.
 */
int task_start_called(void);

/**
 * Enable an interrupt.
 */
void task_enable_irq(int irq);

/**
 * Disable an interrupt.
 */
void task_disable_irq(int irq);

/**
 * Software-trigger an interrupt.
 */
void task_trigger_irq(int irq);

/**
 * Clear a pending interrupt.
 *
 * Note that most interrupts can be removed from the pending state simply by
 * handling whatever caused the interrupt in the first place.  This only needs
 * to be called if an interrupt handler disables itself without clearing the
 * reason for the interrupt, and then the interrupt is re-enabled from a
 * different context.
 */
void task_clear_pending_irq(int irq);

typedef struct k_mutex mutex_t;

#define mutex_lock(mtx) (k_mutex_lock(mtx, K_FOREVER))
#define mutex_unlock(mtx) (k_mutex_unlock(mtx))

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_TASK_H_ */
