/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_WRITE_PROTECT_H
#define __CROS_EC_WRITE_PROTECT_H

#include "gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline bool write_protect_is_asserted(void)
{
#ifdef CONFIG_WP_ALWAYS
	return true;
#elif defined(CONFIG_WP_ACTIVE_HIGH)
	return gpio_get_level(GPIO_WP);
#else
	return !gpio_get_level(GPIO_WP_L);
#endif
}

#ifdef TEST_BUILD
/**
 * Set the WP state.
 *
 * @param value		Logical level of the WP pin
 */
static inline void write_protect_set(int value)
{
#ifdef CONFIG_WP_ACTIVE_HIGH
	gpio_set_level(GPIO_WP, value);
#else
	gpio_set_level(GPIO_WP_L, !value);
#endif /* CONFIG_WP_ACTIVE_HIGH */
}
#endif /* TEST_BUILD */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_WRITE_PROTECT_H */
