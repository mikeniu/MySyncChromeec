/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SUBSYS_HOST_SERVICE_HECI_HECI_SYSTEM_STATE_H_
#define PLATFORM_EC_ZEPHYR_SUBSYS_HOST_SERVICE_HECI_HECI_SYSTEM_STATE_H_

#include "heci_internal.h"

/**
 * Process system state messages. Called by HECI layer when there is
 * system state message arrives
 *
 * ISH registers as AP ISHTP system state client. So AP will send
 * SYSTEM_STATE_QUERY_SUBSCRIBERS and SYSTEM_STATE_STATUS messages
 * to ISH.
 *
 * @param msg:  Payload of heci_bus_msg_t.
 * @param length:  Length of msg.
 */
void heci_handle_system_state_msg(uint8_t *msg, const size_t length);

#endif /* PLATFORM_EC_ZEPHYR_SUBSYS_HOST_SERVICE_HECI_HECI_SYSTEM_STATE_H_ */
