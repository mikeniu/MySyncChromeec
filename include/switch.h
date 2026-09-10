/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Switch module for Chrome EC */

#ifndef PLATFORM_EC_INCLUDE_SWITCH_H_
#define PLATFORM_EC_INCLUDE_SWITCH_H_

#include "common.h"
#include "gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_SWITCH
/**
 * Interrupt handler for switch inputs.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void switch_interrupt(enum gpio_signal signal);
#else
static inline void switch_interrupt(enum gpio_signal signal)
{
}
#endif /* !CONFIG_SWITCH */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_SWITCH_H_ */
