/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "test_common.h"

#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/sys/byteorder.h>

#include <memory>

struct bt_passthru_flow_fixture {
	chre::CrosHostcmdTransport transport;
	chre::ZephyrBtSender sender;
	std::unique_ptr<chre::BtPassthruHciInterceptor> interceptor;

	void Reset()
	{
		RESET_FAKE(mkbp_send_event);
		mock_hci_driver_reset();

		interceptor = std::make_unique<chre::BtPassthruHciInterceptor>(
			transport);

		transport.Start(
			[this](uint32_t /*type*/, pw::span<const uint8_t> data,
			       chre::HostTransport::RespondToHost & /*resp*/) {
				if (interceptor->interceptCommand(
					    data.data(), data.size(),
					    chre::BtClientType::HOST)) {
					return pw::OkStatus();
				}

				sender.sendH4HciPacketToController(data.data(),
								   data.size());
				return pw::OkStatus();
			});

		/* Drain any pending ring buffer items */
		uint8_t drain_buf[512];
		uint32_t count = 0;
		while (chre::CrosHostcmdTransport::GetPendingEvents(
			       drain_buf, sizeof(drain_buf), &count) > 0) {
		}
	}
};

static struct bt_passthru_flow_fixture g_flow_fixture;

static void *flow_setup(void)
{
	return &g_flow_fixture;
}

static void flow_before(void *fixture)
{
	auto *f = static_cast<struct bt_passthru_flow_fixture *>(fixture);
	f->Reset();
}

ZTEST_SUITE(bt_passthru_flow, NULL, flow_setup, flow_before, NULL, NULL);

/* Test end-to-end: Host sends non-intercepted HCI command -> forwarded to
 * Controller */
ZTEST_F(bt_passthru_flow, test_e2e_host_command_to_controller)
{
	/* Prepare HCI Reset command: [H4_CMD(1)] [0x03, 0x0c (Opcode)] [0x00
	 * (Len)] */
	struct ec_param_bt_command req = {};
	req.size = 4;
	req.data[0] = BT_HCI_H4_CMD;
	req.data[1] = 0x03;
	req.data[2] = 0x0c;
	req.data[3] = 0x00;

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_BT_COMMAND, 0, req);
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	/* Verify forwarded to mock HCI controller */
	zassert_equal(mock_hci_driver_get_tx_count(), 1);
	auto tx_pkt = mock_hci_driver_get_last_tx_packet_vec();
	zassert_equal(tx_pkt.size(), req.size);
	zassert_mem_equal(tx_pkt.data(), req.data, req.size);

	/* Verify no MKBP event was generated */
	zassert_equal(mkbp_send_event_fake.call_count, 0);
}

/* Test end-to-end: Host sends 0x1002 -> intercepted & synthesized event read
 * via Host Command */
ZTEST_F(bt_passthru_flow, test_e2e_read_supported_commands_interception)
{
	/* Prepare 0x1002 HCI command */
	struct ec_param_bt_command req = {};
	req.size = 4;
	req.data[0] = BT_HCI_H4_CMD;
	req.data[1] = BT_HCI_OP_READ_SUPPORTED_COMMANDS & 0xff;
	req.data[2] = (BT_HCI_OP_READ_SUPPORTED_COMMANDS >> 8) & 0xff;
	req.data[3] = 0x00;

	struct host_cmd_handler_args cmd_args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_BT_COMMAND, 0, req);
	zassert_equal(host_command_process(&cmd_args), EC_RES_SUCCESS);

	/* Verify NOT forwarded to mock HCI controller */
	zassert_equal(mock_hci_driver_get_tx_count(), 0);

	/* Verify MKBP event was generated */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_BLUETOOTH);

	/* Host polls EC_CMD_BT_READ_EVENT */
	struct ec_response_bt_read_event rsp = {};
	struct host_cmd_handler_args read_args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_BT_READ_EVENT, 0, rsp);
	zassert_equal(host_command_process(&read_args), EC_RES_SUCCESS);

	zassert_equal(rsp.num_events, 1);
	uint8_t event_len = rsp.events[0];
	zassert_equal(event_len,
		      1 + sizeof(struct bt_hci_evt_hdr) +
			      sizeof(struct bt_hci_evt_cmd_complete) +
			      sizeof(struct bt_hci_rp_read_supported_commands));
	const uint8_t *evt_payload = &rsp.events[1];

	/* Verify synthesized event format */
	zassert_equal(evt_payload[0], BT_HCI_H4_EVT);
	const auto *evt_hdr = reinterpret_cast<const struct bt_hci_evt_hdr *>(
		&evt_payload[1]);
	zassert_equal(evt_hdr->evt, BT_HCI_EVT_CMD_COMPLETE);

	const auto *cc_hdr =
		reinterpret_cast<const struct bt_hci_evt_cmd_complete *>(
			evt_hdr->data);
	zassert_equal(sys_le16_to_cpu(cc_hdr->opcode),
		      BT_HCI_OP_READ_SUPPORTED_COMMANDS);

	const auto *rp = reinterpret_cast<
		const struct bt_hci_rp_read_supported_commands *>(
		evt_hdr->data + sizeof(struct bt_hci_evt_cmd_complete));
	zassert_equal(rp->status, 0);
	zassert_equal(rp->commands[36], 0x3e);
	zassert_equal(rp->commands[37], 0x60);
}

/* Test end-to-end: Controller RX packet -> RX thread -> RingBuffer -> MKBP ->
 * Read Event */
ZTEST_F(bt_passthru_flow, test_e2e_controller_rx_to_host)
{
	/* Inject an HCI Event: e.g. LE Advertising Report (H4_EVT + EvtHdr +
	 * payload) */
	uint8_t rx_evt[] = {
		BT_HCI_H4_EVT, BT_HCI_EVT_LE_META_EVENT,
		0x0c, /* Parameter total length */
		0x02, /* Subevent: LE Advertising Report */
		0x01, /* Num reports */
		0x00, /* Event type */
		0x00, /* Addr type */
		0x11,	       0x22,
		0x33,	       0x44,
		0x55,	       0x66, /* BD ADDR */
		0x00, /* Length data */
		0xce, /* RSSI */
	};

	zassert_ok(mock_hci_driver_inject_rx(rx_evt, sizeof(rx_evt)));

	/* Yield/sleep to let RX thread process the buffer */
	k_sleep(K_MSEC(20));

	/* Verify MKBP event was generated */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_BLUETOOTH);

	/* Host polls EC_CMD_BT_READ_EVENT */
	struct ec_response_bt_read_event rsp = {};
	struct host_cmd_handler_args read_args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_BT_READ_EVENT, 0, rsp);
	zassert_equal(host_command_process(&read_args), EC_RES_SUCCESS);

	zassert_equal(rsp.num_events, 1);
	zassert_equal(rsp.events[0], sizeof(rx_evt));
	zassert_mem_equal(&rsp.events[1], rx_evt, sizeof(rx_evt));
}

/* Test end-to-end: Full bidirectional Command -> Controller TX -> Controller RX
 * -> Read Event */
ZTEST_F(bt_passthru_flow, test_e2e_bidirectional_flow)
{
	/* Step 1: AP sends HCI Reset command */
	struct ec_param_bt_command req = {};
	req.size = 4;
	req.data[0] = BT_HCI_H4_CMD;
	req.data[1] = 0x03;
	req.data[2] = 0x0c;
	req.data[3] = 0x00;

	struct host_cmd_handler_args cmd_args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_BT_COMMAND, 0, req);
	zassert_equal(host_command_process(&cmd_args), EC_RES_SUCCESS);

	/* Verify controller received command */
	zassert_equal(mock_hci_driver_get_tx_count(), 1);

	/* Step 2: Controller responds with Command Complete event for Reset */
	uint8_t reset_complete_evt[] = {
		BT_HCI_H4_EVT, BT_HCI_EVT_CMD_COMPLETE,
		0x04, /* len */
		0x01, /* ncmd */
		0x03,	       0x0c, /* opcode 0x0c03 */
		0x00, /* status success */
	};
	zassert_ok(mock_hci_driver_inject_rx(reset_complete_evt,
					     sizeof(reset_complete_evt)));

	/* Yield/sleep for RX thread */
	k_sleep(K_MSEC(20));

	/* Verify MKBP notification */
	zassert_equal(mkbp_send_event_fake.call_count, 1);

	/* Step 3: AP reads event */
	struct ec_response_bt_read_event rsp = {};
	struct host_cmd_handler_args read_args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_BT_READ_EVENT, 0, rsp);
	zassert_equal(host_command_process(&read_args), EC_RES_SUCCESS);

	zassert_equal(rsp.num_events, 1);
	zassert_equal(rsp.events[0], sizeof(reset_complete_evt));
	zassert_mem_equal(&rsp.events[1], reset_complete_evt,
			  sizeof(reset_complete_evt));
}

/* Test end-to-end: Burst of multiple RX packets from Controller */
ZTEST_F(bt_passthru_flow, test_e2e_burst_controller_rx)
{
	uint8_t evt1[] = { BT_HCI_H4_EVT, 0x01, 0x02, 0xaa };
	uint8_t evt2[] = { BT_HCI_H4_EVT, 0x02, 0x03, 0xbb, 0xcc };
	uint8_t evt3[] = { BT_HCI_H4_EVT, 0x03, 0x01, 0xdd };

	zassert_ok(mock_hci_driver_inject_rx(evt1, sizeof(evt1)));
	zassert_ok(mock_hci_driver_inject_rx(evt2, sizeof(evt2)));
	zassert_ok(mock_hci_driver_inject_rx(evt3, sizeof(evt3)));

	/* Sleep for thread processing */
	k_sleep(K_MSEC(30));

	/* Verify MKBP was called */
	zassert_true(mkbp_send_event_fake.call_count >= 1);

	/* Read all events in one batch */
	struct ec_response_bt_read_event rsp = {};
	struct host_cmd_handler_args read_args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_BT_READ_EVENT, 0, rsp);
	zassert_equal(host_command_process(&read_args), EC_RES_SUCCESS);

	zassert_equal(rsp.num_events, 3);

	size_t offset = 0;
	zassert_equal(rsp.events[offset++], sizeof(evt1));
	zassert_mem_equal(&rsp.events[offset], evt1, sizeof(evt1));
	offset += sizeof(evt1);

	zassert_equal(rsp.events[offset++], sizeof(evt2));
	zassert_mem_equal(&rsp.events[offset], evt2, sizeof(evt2));
	offset += sizeof(evt2);

	zassert_equal(rsp.events[offset++], sizeof(evt3));
	zassert_mem_equal(&rsp.events[offset], evt3, sizeof(evt3));
}
