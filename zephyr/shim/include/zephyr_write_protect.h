<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_WRITE_PROTECT_H
#define __CROS_EC_ZEPHYR_WRITE_PROTECT_H

#include "gpio/gpio_int.h"
#include "gpio_signal.h"

#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

int write_protect_is_asserted_custom(void);

/**
 * Check the WP state. The function depends on the alias 'gpio_wp'. It is used
 * to replace the enum-name.
 *
 * @return true if the WP is active, false otherwise.
 */
static inline int write_protect_is_asserted(void)
{
#ifdef CONFIG_WP_ALWAYS
	return true;
#elif CONFIG_PLATFORM_EC_WP_CUSTOM
	return write_protect_is_asserted_custom();
#else
	/* Read write protect GPIO */
	return gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_wp));
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ZEPHYR_WRITE_PROTECT_H */
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_WRITE_PROTECT_H
#define __CROS_EC_ZEPHYR_WRITE_PROTECT_H

#include "gpio/gpio_int.h"
#include "gpio_signal.h"

#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Check the WP state. The function depends on the alias 'gpio_wp'. It is used
 * to replace the enum-name.
 *
 * @return true if the WP is active, false otherwise.
 */
static inline int write_protect_is_asserted(void)
{
#ifdef CONFIG_WP_ALWAYS
	return true;
#else
	/* Read write protect GPIO */
	return gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_wp));
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ZEPHYR_WRITE_PROTECT_H */
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
