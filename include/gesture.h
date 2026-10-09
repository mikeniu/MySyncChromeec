/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header for gesture.c */

#ifndef PLATFORM_EC_INCLUDE_GESTURE_H_
#define PLATFORM_EC_INCLUDE_GESTURE_H_

/**
 * Run gesture detection engine. Modify the event flag when gestures are found.
 */
void gesture_calc(uint32_t *event);

/* gesture hooks are triggered after the motion sense hooks. */
#define GESTURE_HOOK_PRIO (MOTION_SENSE_HOOK_PRIO + 10)

/* Output datarate for tap sensor (in milli-Hz) */
/*
 * Note: lsm6ds0 accel needs twice the expected data rate in order to guarantee
 * that we have a new data sample every reading.
 */
#define TAP_ODR (1000000 / CONFIG_GESTURE_SAMPLING_INTERVAL_MS)
#define TAP_ODR_LSM6DS0 (2 * TAP_ODR)

#endif /* PLATFORM_EC_INCLUDE_GESTURE_H_ */
