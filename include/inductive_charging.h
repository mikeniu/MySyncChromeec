/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inductive charging control */

#include "gpio.h"

#ifndef INCLUDE_INDUCTIVE_CHARGING_H_
#define INCLUDE_INDUCTIVE_CHARGING_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Interrupt handler for inductive charging signal.
 *
 * @param signal  Signal which triggered the interrupt.
 */
void inductive_charging_interrupt(enum gpio_signal);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_INDUCTIVE_CHARGING_H_ */
