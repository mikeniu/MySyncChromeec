/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_GPIO_SIGNAL_H_
#define PLATFORM_EC_INCLUDE_GPIO_SIGNAL_H_

#include "compile_time_macros.h"

#ifndef HOST_TOOLS_BUILD
#include "zephyr_gpio_signal.h"
#else
enum gpio_signal {
	GPIO_UNIMPLEMENTED = -1,
	GPIO_COUNT,
};
#endif

#endif /* PLATFORM_EC_INCLUDE_GPIO_SIGNAL_H_ */
