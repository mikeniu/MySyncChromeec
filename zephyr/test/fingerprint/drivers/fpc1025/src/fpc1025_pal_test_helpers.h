/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_DRIVERS_FPC1025_SRC_FPC1025_PAL_TEST_HELPERS_H_
#define PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_DRIVERS_FPC1025_SRC_FPC1025_PAL_TEST_HELPERS_H_

#include <zephyr/kernel.h>

__syscall int fpc1025_pal_spi_write_read(uint8_t *write, uint8_t *read,
					 size_t size, bool leave_cs_asserted);
__syscall bool fpc1025_pal_spi_check_irq(void);
__syscall bool fpc1025_pal_spi_read_irq(void);
__syscall void fpc1025_pal_spi_reset(bool state);
__syscall uint32_t fpc1025_pal_timebase_get_tick(void);
__syscall void fpc1025_pal_timebase_busy_wait(uint32_t ms);
__syscall void *fpc1025_pal_malloc(uint32_t size);
__syscall void fpc1025_pal_free(void *data);

#include <zephyr/syscalls/fpc1025_pal_test_helpers.h>

#endif /* PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_DRIVERS_FPC1025_SRC_FPC1025_PAL_TEST_HELPERS_H_ \
	*/
