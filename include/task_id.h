/* Copyright 2011 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_TASK_ID_H_
#define PLATFORM_EC_INCLUDE_TASK_ID_H_

#ifndef HOST_TOOLS_BUILD
#include "shimmed_task_id.h"
#else

/*
 * Host tools include headers that reference task IDs but have no tasks.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Task identifier (8 bits) */
typedef uint8_t task_id_t;

enum {
	TASK_ID_IDLE,
	/* Number of tasks */
	TASK_ID_COUNT,
	/* Special task identifiers */
	TASK_ID_INVALID = 0xff, /* unable to find the task */
};

#ifdef __cplusplus
}
#endif
#endif /* !HOST_TOOLS_BUILD */

#endif /* PLATFORM_EC_INCLUDE_TASK_ID_H_ */
