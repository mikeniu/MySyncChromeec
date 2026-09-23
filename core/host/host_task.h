/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator task scheduling module */

#ifndef PLATFORM_EC_CORE_HOST_HOST_TASK_H_
#define PLATFORM_EC_CORE_HOST_HOST_TASK_H_

#include "task.h"

#include <pthread.h>

/**
 * Returns the thread corresponding to the task.
 */
pthread_t task_get_thread(task_id_t tskid);

/**
 * Returns the ID of the active task, regardless of current thread
 * context.
 */
task_id_t task_get_running(void);

/**
 * Initializes the interrupt semaphore and associates a signal handler with
 * SIGNAL_INTERRUPT.
 */
void task_register_interrupt(void);

/**
 * Returns the process ID of the calling process.
 */
pid_t getpid(void);

#endif /* PLATFORM_EC_CORE_HOST_HOST_TASK_H_ */
