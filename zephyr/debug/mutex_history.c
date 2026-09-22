/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "debug/mutex_history.h"

#include <zephyr/spinlock.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/time_units.h>

// Spinlock to protect ring buffer access
static struct k_spinlock rb_lock;

// Function to log a mutex event, overwriting the oldest if full
void mutex_history_log(struct ring_buf *rb, const struct k_mutex *mutex,
		       enum mutex_event type, const char *func)
{
	mutex_event_log_t event = {
		.timestamp = k_cyc_to_us_near32(k_cycle_get_32()),
		.mutex = mutex,
		.thread_id = k_current_get(),
		.event_type = type,
		.func = func,
	};

	k_spinlock_key_t key = k_spin_lock(&rb_lock);

	// Check if space is available
	size_t space = ring_buf_space_get(rb);

	if (space < MUTEX_EVENT_TYPE_SIZE) {
		// Not enough space, drop the oldest event to make room
		ring_buf_consume(rb, MUTEX_EVENT_TYPE_SIZE);
	}

	// Put the new event
	ring_buf_put(rb, (const uint8_t *)&event, MUTEX_EVENT_TYPE_SIZE);

	k_spin_unlock(&rb_lock, key);
}

// Call this from watchdog timeout
void mutex_history_dump(struct ring_buf *rb)
{
	mutex_event_log_t event;
	k_spinlock_key_t key = k_spin_lock(&rb_lock);

	printk("Mutex Event History:\n");
	while (ring_buf_get(rb, (uint8_t *)&event, MUTEX_EVENT_TYPE_SIZE) ==
	       MUTEX_EVENT_TYPE_SIZE) {
		printk("  TS: %8u us, Mutex: %p, Thread: %s, Func: %s, Type: %u\n",
		       event.timestamp, event.mutex,
		       k_thread_name_get(event.thread_id), event.func,
		       event.event_type);
	}

	k_spin_unlock(&rb_lock, key);
}
