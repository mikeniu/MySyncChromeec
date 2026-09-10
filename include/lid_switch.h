/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lid switch API for Chrome EC */

#ifndef PLATFORM_EC_INCLUDE_LID_SWITCH_H_
#define PLATFORM_EC_INCLUDE_LID_SWITCH_H_

#include "common.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return non-zero if lid is open.
 *
 * Uses the debounced lid state, not the raw signal from the GPIO.
 */
int lid_is_open(void);

/**
 * Interrupt handler for lid switch.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void lid_interrupt(enum gpio_signal signal);

/**
 * Disable lid interrupt and set the lid open, when base is disconnected.
 *
 * @param enable    Flag that enables or disables lid interrupt.
 */
void enable_lid_detect(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_LID_SWITCH_H_ */
