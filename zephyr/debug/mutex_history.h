/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Debug buffer of mutex history. Used for debugging deadlock scenarios.
 * Declare a buffer and invoke mutex_history_log(...) to capture mutex events.
 *
 * Add mutex_history_dump to wdt_warning_handler() to trace locking/unlocking of
 * mutexes of interest.
 */

#ifndef PLATFORM_EC_ZEPHYR_DEBUG_MUTEX_HISTORY_H_
#define PLATFORM_EC_ZEPHYR_DEBUG_MUTEX_HISTORY_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

enum mutex_event {
	LOCKING = 0,
	LOCKED = 1,
	UNLOCKED = 2,
	INIT = 3,
	BREADCRUMB = 4,
};

// Define the structure for each event
typedef struct {
	uint32_t timestamp;
	const struct k_mutex *mutex;
	k_tid_t thread_id;
	enum mutex_event event_type;
	const char *func;
} mutex_event_log_t;

#define MUTEX_EVENT_TYPE_SIZE sizeof(mutex_event_log_t)

/**
 * @brief Define and initialize an Mutex history ring buffer.
 *
 *
 * @param name Name of the mutex history buffer.
 * @param size Size of mutex history buffer
 */
#define MUTEX_HISTORY_DECLARE(name, size) \
	RING_BUF_ITEM_DECLARE(name, (MUTEX_EVENT_TYPE_SIZE * size))

/**
 * @brief Log mutex event in mutex history buffer
 *
 * @param rb 	Pointer to mutex history buffer
 * @param mutex Mutex that is being acted upon
 * @param type	Mutex event
 * @param func  Name of function locking/unlocking the mutex
 */
void mutex_history_log(struct ring_buf *rb, const struct k_mutex *mutex,
		       enum mutex_event type, const char *func);

/* Helper macro for logging mutex history */
#define MUTEX_HISTORY_LOG(rb, mutex, type) \
	mutex_history_log(rb, mutex, type, __func__)

#define MUTEX_HISTORY_LOG_CRUMB(rb, str) \
	mutex_history_log(rb, NULL, BREADCRUMB, STRINGIFY(__LINE__) ": " str)

#define MUTEX_HISTORY_DROP_CRUMB(rb)            \
	mutex_history_log(rb, NULL, BREADCRUMB, \
			  __FILE__ ":" STRINGIFY(__LINE__))

/**
 * @brief Dump the contents of mutex history buffer to console
 *
 * @param rb Pointer to mutex history buffer
 */
void mutex_history_dump(struct ring_buf *rb);

#endif /* PLATFORM_EC_ZEPHYR_DEBUG_MUTEX_HISTORY_H_ */
