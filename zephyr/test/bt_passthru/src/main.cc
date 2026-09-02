/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "test_common.h"

extern "C" {
DEFINE_FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);
}

static void *bt_passthru_setup(void)
{
	return nullptr;
}

static void bt_passthru_before(void *fixture)
{
	ARG_UNUSED(fixture);
	mock_hci_driver_reset();
	RESET_FAKE(mkbp_send_event);
}

ZTEST_SUITE(bt_boot_init, NULL, bt_passthru_setup, bt_passthru_before, NULL,
	    NULL);

ZTEST(bt_boot_init, test_mock_driver_initialized)
{
	/* Verify that the mock driver was opened and ready */
	zassert_equal(mock_hci_driver_get_tx_count(), 0);
}

/* Test global SYS_INIT TX handler forwarding to controller */
ZTEST(bt_boot_init, test_sys_init_tx_forwarding)
{
	/* HCI Reset command */
	struct ec_param_bt_command req = {};
	req.size = 4;
	req.data[0] = BT_HCI_H4_CMD;
	req.data[1] = 0x03;
	req.data[2] = 0x0c;
	req.data[3] = 0x00;

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_BT_COMMAND, 0, req);
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* Verify forwarded to mock HCI controller via SYS_INIT lambda */
	zassert_equal(mock_hci_driver_get_tx_count(), 1);
	auto tx_pkt = mock_hci_driver_get_last_tx_packet_vec();
	zassert_equal(tx_pkt.size(), req.size);
	zassert_mem_equal(tx_pkt.data(), req.data, req.size);
}

/* Test global SYS_INIT TX handler intercepting 0x1002 */
ZTEST(bt_boot_init, test_sys_init_tx_intercept)
{
	/* HCI Read Local Supported Commands: [H4_CMD] [0x02, 0x10] [0x00] */
	struct ec_param_bt_command req = {};
	req.size = 4;
	req.data[0] = BT_HCI_H4_CMD;
	req.data[1] = BT_HCI_OP_READ_SUPPORTED_COMMANDS & 0xff;
	req.data[2] = (BT_HCI_OP_READ_SUPPORTED_COMMANDS >> 8) & 0xff;
	req.data[3] = 0x00;

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_BT_COMMAND, 0, req);
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* Verify intercepted: not forwarded to controller, MKBP triggered */
	zassert_equal(mock_hci_driver_get_tx_count(), 0);
	zassert_equal(mkbp_send_event_fake.call_count, 1);
}

/* Test RX thread consuming empty / 0-length buffer */
ZTEST(bt_boot_init, test_rx_empty_buffer)
{
	zassert_ok(mock_hci_driver_inject_empty_rx());

	/* Yield to let RX thread process the empty buffer */
	k_sleep(K_MSEC(20));

	/* Verify no MKBP event was generated */
	zassert_equal(mkbp_send_event_fake.call_count, 0);
}
