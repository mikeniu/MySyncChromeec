/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_DRIVERS_ELAN_SERIES_SRC_ELAN_SERIES_PAL_TEST_HELPERS_H_
#define PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_DRIVERS_ELAN_SERIES_SRC_ELAN_SERIES_PAL_TEST_HELPERS_H_

#include <zephyr/kernel.h>

#include <fingerprint_elan_series_pal.h>

__syscall int elan_series_pal_usleep(unsigned int us);
__syscall void *elan_series_pal_malloc(uint32_t size);
__syscall void elan_series_pal_free(void *data);
__syscall uint32_t elan_series_pal_get_tick(void);
__syscall void elan_series_pal_sensor_set_rst(bool state);
__syscall int elan_series_pal_read_register(uint8_t regaddr, uint8_t *regdata);
__syscall int elan_series_pal_read_cmd(uint8_t fp_cmd, uint8_t *regdata);

#include <zephyr/syscalls/elan_series_pal_test_helpers.h>

#endif /* PLATFORM_EC_ZEPHYR_TEST_FINGERPRINT_DRIVERS_ELAN_SERIES_SRC_ELAN_SERIES_PAL_TEST_HELPERS_H_ \
	*/
