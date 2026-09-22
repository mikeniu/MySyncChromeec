/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if !defined(PLATFORM_EC_INCLUDE_HOST_COMMAND_H_) || \
	defined(PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_ZEPHYR_HOST_COMMAND_H_)
#error "This file must only be included from host_command.h. " \
	"Include host_command.h directly"
#endif

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_ZEPHYR_HOST_COMMAND_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_ZEPHYR_HOST_COMMAND_H_

#include <stdbool.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initializes and runs the host command handler loop.  */
void host_command_task(void *u);

/* Takes over the main thread and runs the host command loop. */
void host_command_main(void);

/*
 * Returns the main thread id. Will be the same as the HOSTCMD thread
 * when CONFIG_TASK_HOSTCMD_THREAD_MAIN is enabled.
 */
k_tid_t get_main_thread(void);

/*
 * Returns the HOSTCMD thread id. Will be different than the main thread
 * when CONFIG_TASK_HOSTCMD_THREAD_DEDICATED is enabled.
 */
k_tid_t get_hostcmd_thread(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_ZEPHYR_HOST_COMMAND_H_ */
