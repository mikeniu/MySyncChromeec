/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_BT_PASSTHRU_TEST_COMMON_H_
#define ZEPHYR_TEST_BT_PASSTHRU_TEST_COMMON_H_

#include "bt_passthru_hci_interceptor.h"
#include "cros_hostcmd_transport.h"
#include "ec_commands.h"
#include "host_command.h"
#include "mock_hci_driver.h"
#include "zephyr_bt_sender.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

extern "C" {
DECLARE_FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);
}

#endif /* ZEPHYR_TEST_BT_PASSTHRU_TEST_COMMON_H_ */
