/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timer driver API */

#ifndef PLATFORM_EC_INCLUDE_HWTIMER_H_
#define PLATFORM_EC_INCLUDE_HWTIMER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the timestamp of the next programed event */
uint32_t __hw_clock_event_get(void);

/**
 * Get the lower 32-bits of the free-running counter used as clock
 *
 * The counter resolution must be 1us, since udelay() relies on this.
 *
 * @return current counter value
 */
#ifdef CONFIG_HWTIMER_64BIT
__override_proto
#endif
	uint32_t __hw_clock_source_read(void);

/**
 * Get the 64-bit value of the free-running counter used as clock,
 * only available when CONFIG_HWTIMER_64BIT is enabled.
 *
 * This function should only be used by common/timer.c or
 * chip-specific code, as get_time() abstracts the config option away.
 */
uint64_t __hw_clock_source_read64(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_HWTIMER_H_ */
