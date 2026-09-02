/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Internal API for sending AP power related event callbacks.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_AP_POWER_AP_POWER_EVENTS_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_AP_POWER_AP_POWER_EVENTS_H_

#include <ap_power/ap_power.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Dispatch callbacks for an event(s).
 *
 * The event parameter should be a bitmap by or'ing one or more
 * ap_power_events.
 *
 * @param event The event(s) to invoke callbacks for.
 */
void ap_power_ev_send_callbacks(uint32_t event);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_AP_POWER_AP_POWER_EVENTS_H_ */
