/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "test_common.h"

#include <vector>

struct cros_hostcmd_transport_fixture {
	chre::CrosHostcmdTransport transport;
	std::vector<uint8_t> last_inbound_cmd;
	pw::Status inbound_status = pw::OkStatus();

	void Reset()
	{
		last_inbound_cmd.clear();
		inbound_status = pw::OkStatus();
		RESET_FAKE(mkbp_send_event);
		mock_hci_driver_reset();

		/* Re-initialize transport */
		transport.Start(
			[this](uint32_t /*type*/,
			       pw::span<const uint8_t> payload,
			       chre::HostTransport::RespondToHost & /*cb*/) {
				last_inbound_cmd.assign(payload.begin(),
							payload.end());
				return inbound_status;
			});

		/* Drain any pending ring buffer items */
		uint8_t drain_buf[512];
		uint32_t count = 0;
		while (chre::CrosHostcmdTransport::GetPendingEvents(
			       drain_buf, sizeof(drain_buf), &count) > 0) {
		}
	}
};

static struct cros_hostcmd_transport_fixture g_fixture;

static void *hostcmd_transport_setup(void)
{
	return &g_fixture;
}

static void hostcmd_transport_before(void *fixture)
{
	auto *f = static_cast<struct cros_hostcmd_transport_fixture *>(fixture);
	f->Reset();
}

ZTEST_SUITE(cros_hostcmd_transport, NULL, hostcmd_transport_setup,
	    hostcmd_transport_before, NULL, NULL);

/* Test Start() parameter validation */
ZTEST_F(cros_hostcmd_transport, test_start_validation)
{
	chre::CrosHostcmdTransport t;
	zassert_equal(t.Start(nullptr), pw::Status::InvalidArgument());
}

/* Test Write() parameter validation */
ZTEST_F(cros_hostcmd_transport, test_write_validation)
{
	/* Empty data */
	zassert_equal(fixture->transport.Write(0, {}, false),
		      pw::Status::InvalidArgument());

	/* Oversized data > 255 bytes */
	uint8_t huge_buf[256] = {};
	zassert_equal(fixture->transport.Write(0, huge_buf, false),
		      pw::Status::InvalidArgument());
}

/* Test Write() with wakeUp=true triggers MKBP */
ZTEST_F(cros_hostcmd_transport, test_write_wakeup_triggers_mkbp)
{
	uint8_t evt[] = { 0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00 };
	zassert_equal(fixture->transport.Write(0, evt, true), pw::OkStatus());

	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_BLUETOOTH);

	/* Read event via GetPendingEvents */
	uint8_t out_buf[64] = {};
	uint32_t num_events = 0;
	size_t bytes = chre::CrosHostcmdTransport::GetPendingEvents(
		out_buf, sizeof(out_buf), &num_events);

	zassert_equal(num_events, 1);
	zassert_equal(bytes, sizeof(evt) + 1); /* 1 length byte + payload */
	zassert_equal(out_buf[0], sizeof(evt));
	zassert_mem_equal(&out_buf[1], evt, sizeof(evt));
}

/* Test Write() with wakeUp=false does NOT trigger MKBP */
ZTEST_F(cros_hostcmd_transport, test_write_no_wakeup_skips_mkbp)
{
	uint8_t evt[] = { 0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00 };
	zassert_equal(fixture->transport.Write(0, evt, false), pw::OkStatus());

	zassert_equal(mkbp_send_event_fake.call_count, 0);

	/* Events are still queued */
	uint8_t out_buf[64] = {};
	uint32_t num_events = 0;
	size_t bytes = chre::CrosHostcmdTransport::GetPendingEvents(
		out_buf, sizeof(out_buf), &num_events);

	zassert_equal(num_events, 1);
	zassert_equal(bytes, sizeof(evt) + 1);
}

/* Test FIFO ordering and packing of multiple events */
ZTEST_F(cros_hostcmd_transport, test_fifo_multiple_events)
{
	uint8_t evt1[] = { 0x01, 0x02 };
	uint8_t evt2[] = { 0x03, 0x04, 0x05 };
	uint8_t evt3[] = { 0x06, 0x07, 0x08, 0x09 };

	zassert_equal(fixture->transport.Write(0, evt1, false), pw::OkStatus());
	zassert_equal(fixture->transport.Write(0, evt2, false), pw::OkStatus());
	zassert_equal(fixture->transport.Write(0, evt3, false), pw::OkStatus());

	uint8_t out_buf[64] = {};
	uint32_t num_events = 0;
	size_t bytes = chre::CrosHostcmdTransport::GetPendingEvents(
		out_buf, sizeof(out_buf), &num_events);

	zassert_equal(num_events, 3);
	size_t expected_total =
		(1 + sizeof(evt1)) + (1 + sizeof(evt2)) + (1 + sizeof(evt3));
	zassert_equal(bytes, expected_total);

	/* Check unpacking: len + data */
	size_t idx = 0;
	zassert_equal(out_buf[idx++], sizeof(evt1));
	zassert_mem_equal(&out_buf[idx], evt1, sizeof(evt1));
	idx += sizeof(evt1);

	zassert_equal(out_buf[idx++], sizeof(evt2));
	zassert_mem_equal(&out_buf[idx], evt2, sizeof(evt2));
	idx += sizeof(evt2);

	zassert_equal(out_buf[idx++], sizeof(evt3));
	zassert_mem_equal(&out_buf[idx], evt3, sizeof(evt3));
}

/* Test partial read when output buffer cannot fit all events */
ZTEST_F(cros_hostcmd_transport, test_partial_buffer_packing)
{
	uint8_t evt1[20] = { 0x11 };
	uint8_t evt2[20] = { 0x22 };

	zassert_equal(fixture->transport.Write(0, evt1, false), pw::OkStatus());
	zassert_equal(fixture->transport.Write(0, evt2, false), pw::OkStatus());

	/* Out buffer can fit only 1 event: 1 + 20 = 21 bytes */
	uint8_t out_buf[30] = {};
	uint32_t num_events = 0;
	size_t bytes = chre::CrosHostcmdTransport::GetPendingEvents(
		out_buf, 25, &num_events);

	zassert_equal(num_events, 1);
	zassert_equal(bytes, 21);
	zassert_equal(out_buf[0], 20);

	/* Next read retrieves the second event */
	bytes = chre::CrosHostcmdTransport::GetPendingEvents(
		out_buf, sizeof(out_buf), &num_events);
	zassert_equal(num_events, 1);
	zassert_equal(bytes, 21);
	zassert_equal(out_buf[0], 20);
}

/* Test deadlock protection: single event larger than max_size is dropped */
ZTEST_F(cros_hostcmd_transport, test_deadlock_protection)
{
	uint8_t huge_evt[100] = { 0xaa };
	uint8_t normal_evt[10] = { 0xbb };

	zassert_equal(fixture->transport.Write(0, huge_evt, false),
		      pw::OkStatus());
	zassert_equal(fixture->transport.Write(0, normal_evt, false),
		      pw::OkStatus());

	/* max_size is 50, so huge_evt (101 bytes) cannot fit and must be
	 * discarded */
	uint8_t out_buf[50] = {};
	uint32_t num_events = 0;
	size_t bytes = chre::CrosHostcmdTransport::GetPendingEvents(
		out_buf, sizeof(out_buf), &num_events);

	zassert_equal(num_events, 1);
	zassert_equal(bytes, 11); /* 1 length byte + 10 normal_evt */
	zassert_equal(out_buf[0], 10);
	zassert_mem_equal(&out_buf[1], normal_evt, sizeof(normal_evt));
}

/* Test ring buffer overflow returns ResourceExhausted and does not corrupt
 * existing data */
ZTEST_F(cros_hostcmd_transport, test_ring_buf_overflow)
{
	uint8_t pkt[200] = {};
	int successful_writes = 0;

	for (int i = 0; i < 30; i++) {
		pkt[0] = static_cast<uint8_t>(i);
		pw::Status status = fixture->transport.Write(0, pkt, false);
		if (status.ok()) {
			successful_writes++;
		} else {
			zassert_equal(status, pw::Status::ResourceExhausted());
			break;
		}
	}

	zassert_true(successful_writes > 0);

	/* Verify queued data can be drained correctly */
	uint8_t out_buf[512] = {};
	uint32_t total_events = 0;
	uint32_t num_events = 0;
	while (chre::CrosHostcmdTransport::GetPendingEvents(
		       out_buf, sizeof(out_buf), &num_events) > 0) {
		total_events += num_events;
	}
	zassert_equal(total_events, successful_writes);
}

/* Test EC_CMD_BT_COMMAND host command */
ZTEST_F(cros_hostcmd_transport, test_host_command_bt_command)
{
	/* Invalid param: size = 0 */
	struct ec_param_bt_command req = {};
	req.size = 0;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_BT_COMMAND, 0, req);
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);

	/* Invalid param: size > BT_MAX_COMMAND_SIZE */
	req.size = BT_MAX_COMMAND_SIZE + 1;
	args = BUILD_HOST_COMMAND_PARAMS(EC_CMD_BT_COMMAND, 0, req);
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);

	/* Valid command */
	req.size = 3;
	req.data[0] = 0x03;
	req.data[1] = 0x0c;
	req.data[2] = 0x00;
	args = BUILD_HOST_COMMAND_PARAMS(EC_CMD_BT_COMMAND, 0, req);
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);

	zassert_equal(fixture->last_inbound_cmd.size(), 3);
	zassert_equal(fixture->last_inbound_cmd[0], 0x03);
	zassert_equal(fixture->last_inbound_cmd[1], 0x0c);
	zassert_equal(fixture->last_inbound_cmd[2], 0x00);

	/* Error from inbound handler */
	fixture->inbound_status = pw::Status::Internal();
	args = BUILD_HOST_COMMAND_PARAMS(EC_CMD_BT_COMMAND, 0, req);
	zassert_equal(host_command_process(&args), EC_RES_ERROR);
}

/* Test EC_CMD_BT_READ_EVENT host command */
ZTEST_F(cros_hostcmd_transport, test_host_command_bt_read_event)
{
	struct ec_response_bt_read_event rsp = {};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_BT_READ_EVENT, 0, rsp);

	/* Empty ring buffer returns EC_RES_UNAVAILABLE */
	zassert_equal(host_command_process(&args), EC_RES_UNAVAILABLE);

	/* Write an event */
	uint8_t evt[] = { 0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00 };
	zassert_equal(fixture->transport.Write(0, evt, false), pw::OkStatus());

	/* Now read it via host command */
	zassert_equal(host_command_process(&args), EC_RES_SUCCESS);
	zassert_equal(rsp.num_events, 1);
	zassert_equal(rsp.events[0], sizeof(evt));
	zassert_mem_equal(&rsp.events[1], evt, sizeof(evt));
	zassert_equal(args.response_size,
		      sizeof(rsp.num_events) + 1 + sizeof(evt));

	/* Second read returns UNAVAILABLE because buffer was drained */
	zassert_equal(host_command_process(&args), EC_RES_UNAVAILABLE);
}
