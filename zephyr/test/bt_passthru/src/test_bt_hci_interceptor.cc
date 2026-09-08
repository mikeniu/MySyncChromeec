/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bt_passthru_hci_interceptor.h"
#include "test_common.h"

#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/sys/byteorder.h>

#include <memory>

struct bt_hci_interceptor_fixture {
	chre::CrosHostcmdTransport transport;
	std::unique_ptr<chre::BtPassthruHciInterceptor> interceptor;

	void Reset()
	{
		RESET_FAKE(mkbp_send_event);
		transport.Start(
			[](uint32_t /*type*/,
			   pw::span<const uint8_t> /*payload*/,
			   chre::HostTransport::RespondToHost & /*cb*/) {
				return pw::OkStatus();
			});
		interceptor = std::make_unique<chre::BtPassthruHciInterceptor>(
			transport);

		/* Drain pending events */
		uint8_t drain[512];
		uint32_t count = 0;
		while (chre::CrosHostcmdTransport::GetPendingEvents(
			       drain, sizeof(drain), &count) > 0) {
		}
	}
};

static struct bt_hci_interceptor_fixture g_interceptor_fixture;

static void *interceptor_setup(void)
{
	return &g_interceptor_fixture;
}

static void interceptor_before(void *fixture)
{
	auto *f = static_cast<struct bt_hci_interceptor_fixture *>(fixture);
	f->Reset();
}

ZTEST_SUITE(bt_hci_interceptor, NULL, interceptor_setup, interceptor_before,
	    NULL, NULL);

/* Test malformed / invalid command interception rejection */
ZTEST_F(bt_hci_interceptor, test_intercept_invalid_inputs)
{
	/* Null buffer */
	zassert_false(fixture->interceptor->interceptCommand(
		nullptr, 10, chre::BtClientType::HOST));

	/* Length < 4 */
	uint8_t short_buf[3] = { BT_HCI_H4_CMD, 0x03, 0x0c };
	zassert_false(fixture->interceptor->interceptCommand(
		short_buf, sizeof(short_buf), chre::BtClientType::HOST));

	/* Invalid H4 type */
	uint8_t acl_buf[4] = { BT_HCI_H4_ACL, 0x00, 0x00, 0x00 };
	zassert_false(fixture->interceptor->interceptCommand(
		acl_buf, sizeof(acl_buf), chre::BtClientType::HOST));

	/* Truncated param length */
	uint8_t trunc_buf[5] = { BT_HCI_H4_CMD, 0x01, 0x20, 0x05, 0xaa };
	zassert_false(fixture->interceptor->interceptCommand(
		trunc_buf, sizeof(trunc_buf), chre::BtClientType::HOST));
}

/* Test non-intercepted command passes through (returns false) */
ZTEST_F(bt_hci_interceptor, test_passthrough_non_intercepted_command)
{
	/* HCI Reset: [H4_CMD] [0x03, 0x0c] [param_len=0] */
	uint8_t reset_cmd[] = { BT_HCI_H4_CMD, 0x03, 0x0c, 0x00 };
	zassert_false(fixture->interceptor->interceptCommand(
		reset_cmd, sizeof(reset_cmd), chre::BtClientType::HOST));

	/* Verify no events synthesized */
	uint8_t out_buf[64] = {};
	uint32_t num_events = 0;
	size_t bytes = chre::CrosHostcmdTransport::GetPendingEvents(
		out_buf, sizeof(out_buf), &num_events);
	zassert_equal(num_events, 0);
	zassert_equal(bytes, 0);
}

/* Test 0x1002 (Read Local Supported Commands) synthesis */
ZTEST_F(bt_hci_interceptor, test_synthesize_read_supported_commands)
{
	/* HCI Read Local Supported Commands: [H4_CMD] [0x02, 0x10] [0x00] */
	uint8_t read_supp_cmd[] = {
		BT_HCI_H4_CMD,
		BT_HCI_OP_READ_SUPPORTED_COMMANDS & 0xff,
		(BT_HCI_OP_READ_SUPPORTED_COMMANDS >> 8) & 0xff,
		0x00,
	};

	bool intercepted = fixture->interceptor->interceptCommand(
		read_supp_cmd, sizeof(read_supp_cmd), chre::BtClientType::HOST);
	zassert_true(intercepted);

	/* Verify MKBP wake event was triggered */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_BLUETOOTH);

	/* Retrieve synthesized event from ring buffer */
	uint8_t out_buf[128] = {};
	uint32_t num_events = 0;
	size_t total_bytes = chre::CrosHostcmdTransport::GetPendingEvents(
		out_buf, sizeof(out_buf), &num_events);

	zassert_equal(num_events, 1);
	uint8_t event_len = out_buf[0];
	const uint8_t *evt_payload = &out_buf[1];

	/* Expected event size: [H4_EVT(1)] + [bt_hci_evt_hdr(2)] +
	 * [bt_hci_evt_cmd_complete(3)] +
	 * [bt_hci_rp_read_supported_commands(65)] = 71 */
	size_t expected_size = 1 + sizeof(struct bt_hci_evt_hdr) +
			       sizeof(struct bt_hci_evt_cmd_complete) +
			       sizeof(struct bt_hci_rp_read_supported_commands);
	zassert_equal(event_len, expected_size);
	zassert_equal(total_bytes, expected_size + 1);

	/* Check H4 Type */
	zassert_equal(evt_payload[0], BT_HCI_H4_EVT);

	/* Check Event Header */
	const auto *evt_hdr = reinterpret_cast<const struct bt_hci_evt_hdr *>(
		&evt_payload[1]);
	zassert_equal(evt_hdr->evt, BT_HCI_EVT_CMD_COMPLETE);
	zassert_equal(evt_hdr->len,
		      sizeof(struct bt_hci_evt_cmd_complete) +
			      sizeof(struct bt_hci_rp_read_supported_commands));

	/* Check Command Complete Header */
	const auto *cc_hdr =
		reinterpret_cast<const struct bt_hci_evt_cmd_complete *>(
			evt_hdr->data);
	zassert_equal(cc_hdr->ncmd, 1);
	zassert_equal(sys_le16_to_cpu(cc_hdr->opcode),
		      BT_HCI_OP_READ_SUPPORTED_COMMANDS);

	/* Check Return Parameters */
	const auto *rp = reinterpret_cast<
		const struct bt_hci_rp_read_supported_commands *>(
		evt_hdr->data + sizeof(struct bt_hci_evt_cmd_complete));
	zassert_equal(rp->status, 0);

	/* Check Octet 36: bits 1..5 (0x3e) */
	zassert_equal(rp->commands[36], 0x3e);

	/* Check Octet 37: bits 5..6 (0x60) */
	zassert_equal(rp->commands[37], 0x60);

	/* Verify all other octets are 0 */
	for (size_t i = 0; i < sizeof(rp->commands); i++) {
		if (i != 36 && i != 37) {
			zassert_equal(rp->commands[i], 0,
				      "Octet %zu expected 0, got 0x%02x", i,
				      rp->commands[i]);
		}
	}
}

/* Test interceptCommandResponseEvent returns false */
ZTEST_F(bt_hci_interceptor, test_intercept_command_response_event)
{
	uint8_t sample_evt[10] = { BT_HCI_H4_EVT };
	zassert_false(fixture->interceptor->interceptCommandResponseEvent(
		sample_evt, sizeof(sample_evt)));
}
