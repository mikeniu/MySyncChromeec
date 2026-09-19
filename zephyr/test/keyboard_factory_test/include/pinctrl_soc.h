/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_KEYBOARD_FACTORY_TEST_INCLUDE_PINCTRL_SOC_H_
#define PLATFORM_EC_ZEPHYR_TEST_KEYBOARD_FACTORY_TEST_INCLUDE_PINCTRL_SOC_H_

struct pinctrl_soc_pin {
	uint32_t empty;
};

typedef struct pinctrl_soc_pin pinctrl_soc_pin_t;

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop) \
	{                                        \
	}

#endif /* PLATFORM_EC_ZEPHYR_TEST_KEYBOARD_FACTORY_TEST_INCLUDE_PINCTRL_SOC_H_ \
	*/
