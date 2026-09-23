/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_MOCK_INCLUDE_MOCK_AP_POWER_EVENTS_H_
#define PLATFORM_EC_ZEPHYR_MOCK_INCLUDE_MOCK_AP_POWER_EVENTS_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <zephyr/fff.h>

#include <ap_power/ap_power.h>

/* Mocks for ec/zephyr/include/ap_power/ap_power_events.h */
DECLARE_FAKE_VOID_FUNC(ap_power_ev_send_callbacks, enum ap_power_events);

void ap_power_ev_send_callbacks_custom_fake(enum ap_power_events event);

#endif /* PLATFORM_EC_ZEPHYR_MOCK_INCLUDE_MOCK_AP_POWER_EVENTS_H_ */
