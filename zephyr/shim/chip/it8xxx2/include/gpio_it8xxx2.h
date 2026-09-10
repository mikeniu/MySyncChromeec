/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SHIM_CHIP_IT8XXX2_INCLUDE_GPIO_IT8XXX2_H_
#define PLATFORM_EC_ZEPHYR_SHIM_CHIP_IT8XXX2_INCLUDE_GPIO_IT8XXX2_H_

enum gpio_port_to_node {
	GPIO_A,
	GPIO_B,
	GPIO_C,
	GPIO_D,
	GPIO_E,
	GPIO_F,
	GPIO_G,
	GPIO_H,
	GPIO_I,
	GPIO_J,
	GPIO_K,
	GPIO_L,
	GPIO_M,
	GPIO_KSI = 50,
	GPIO_KSOH = 51,
	GPIO_KSOL = 52
};

#endif /* PLATFORM_EC_ZEPHYR_SHIM_CHIP_IT8XXX2_INCLUDE_GPIO_IT8XXX2_H_ */
