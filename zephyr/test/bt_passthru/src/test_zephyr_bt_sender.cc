/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "test_common.h"
#include "zephyr_bt_sender.h"

struct zephyr_bt_sender_fixture {
	chre::ZephyrBtSender sender;

	void Reset()
	{
		mock_hci_driver_reset();
	}
};

static struct zephyr_bt_sender_fixture g_sender_fixture;

static void *sender_setup(void)
{
	return &g_sender_fixture;
}

static void sender_before(void *fixture)
{
	auto *f = static_cast<struct zephyr_bt_sender_fixture *>(fixture);
	f->Reset();
}

ZTEST_SUITE(zephyr_bt_sender, NULL, sender_setup, sender_before, NULL, NULL);

/* Test sending malformed / invalid packets */
ZTEST_F(zephyr_bt_sender, test_send_invalid_packets)
{
	/* Null buffer */
	fixture->sender.sendH4HciPacketToController(nullptr, 10);
	zassert_equal(mock_hci_driver_get_tx_count(), 0);

	/* Length < 4 */
	uint8_t short_pkt[3] = { BT_HCI_H4_CMD, 0x03, 0x0c };
	fixture->sender.sendH4HciPacketToController(short_pkt,
						    sizeof(short_pkt));
	zassert_equal(mock_hci_driver_get_tx_count(), 0);

	/* Unsupported H4 Type (ACL data) */
	uint8_t acl_pkt[5] = { BT_HCI_H4_ACL, 0x01, 0x00, 0x01, 0x00 };
	fixture->sender.sendH4HciPacketToController(acl_pkt, sizeof(acl_pkt));
	zassert_equal(mock_hci_driver_get_tx_count(), 0);

	/* Truncated command payload */
	uint8_t trunc_pkt[5] = { BT_HCI_H4_CMD, 0x01, 0x20, 0x05, 0x11 };
	fixture->sender.sendH4HciPacketToController(trunc_pkt,
						    sizeof(trunc_pkt));
	zassert_equal(mock_hci_driver_get_tx_count(), 0);
}

/* Test sending valid HCI command to controller */
ZTEST_F(zephyr_bt_sender, test_send_valid_hci_command)
{
	/* HCI Reset: [0x01 (H4)] [0x03, 0x0c (Opcode)] [0x00 (Param Len)] */
	uint8_t reset_cmd[] = { BT_HCI_H4_CMD, 0x03, 0x0c, 0x00 };
	fixture->sender.sendH4HciPacketToController(reset_cmd,
						    sizeof(reset_cmd));

	zassert_equal(mock_hci_driver_get_tx_count(), 1);

	auto tx_packet = mock_hci_driver_get_last_tx_packet_vec();
	zassert_equal(tx_packet.size(), sizeof(reset_cmd));
	zassert_mem_equal(tx_packet.data(), reset_cmd, sizeof(reset_cmd));
}

/* Test sending command with parameters */
ZTEST_F(zephyr_bt_sender, test_send_command_with_parameters)
{
	/* HCI LE Set Scan Enable: Opcode 0x200c, param_len 2, params [0x01,
	 * 0x00] */
	uint8_t scan_cmd[] = { BT_HCI_H4_CMD, 0x0c, 0x20, 0x02, 0x01, 0x00 };
	fixture->sender.sendH4HciPacketToController(scan_cmd, sizeof(scan_cmd));

	zassert_equal(mock_hci_driver_get_tx_count(), 1);

	auto tx_packet = mock_hci_driver_get_last_tx_packet_vec();
	zassert_equal(tx_packet.size(), sizeof(scan_cmd));
	zassert_mem_equal(tx_packet.data(), scan_cmd, sizeof(scan_cmd));
}

/* Test driver error handling during send */
ZTEST_F(zephyr_bt_sender, test_send_driver_failure)
{
	mock_hci_driver_set_send_result(-EIO);

	uint8_t reset_cmd[] = { BT_HCI_H4_CMD, 0x03, 0x0c, 0x00 };
	/* Should handle failure gracefully without crashing */
	fixture->sender.sendH4HciPacketToController(reset_cmd,
						    sizeof(reset_cmd));

	/* Since mock driver returned error before recording, TX count is 0 */
	zassert_equal(mock_hci_driver_get_tx_count(), 0);
}
