/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_hostcmd_transport.h"
#include "host_command.h"
#include "mkbp_event.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/ring_buffer.h>

LOG_MODULE_DECLARE(bt_passthru, CONFIG_BT_PASSTHRU_LOG_LEVEL);

namespace chre
{

/* Define the static class members */
uint8_t CrosHostcmdTransport::bt_event_data_[kBtEventBufSize];
struct ring_buf CrosHostcmdTransport::bt_events_ring_buf_;
struct k_spinlock CrosHostcmdTransport::rb_lock_;
HostTransport::HandleHostMsg CrosHostcmdTransport::msg_handler_ = nullptr;

pw::Status CrosHostcmdTransport::Start(HandleHostMsg &&msgHandler)
{
	if (!msgHandler) {
		return pw::Status::InvalidArgument();
	}

	ring_buf_init(&bt_events_ring_buf_, sizeof(bt_event_data_),
		      bt_event_data_);
	msg_handler_ = std::move(msgHandler);

	LOG_INF("CrosHostcmdTransport initialized");
	return pw::OkStatus();
}

pw::Status CrosHostcmdTransport::Write(uint32_t /* type */,
				       pw::span<const uint8_t> data,
				       bool wakeUp)
{
	if (data.empty() || data.size() > 255) {
		LOG_ERR("HCI Event too large for transport: %zu bytes",
			data.size());
		return pw::Status::InvalidArgument();
	}

	LOG_HEXDUMP_DBG(data.data(), data.size(), "HCI Event -> RingBuf");

	uint8_t len = static_cast<uint8_t>(data.size());
	bool dropped = false;

	k_spinlock_key_t key = k_spin_lock(&rb_lock_);
	if (ring_buf_space_get(&bt_events_ring_buf_) <
	    static_cast<uint32_t>(len + 1)) {
		dropped = true;
	} else {
		ring_buf_put(&bt_events_ring_buf_, &len, 1);
		ring_buf_put(&bt_events_ring_buf_, data.data(), len);
	}
	k_spin_unlock(&rb_lock_, key);

	if (dropped) {
		LOG_WRN("BT Event Ring Buffer full, dropping packet");
		return pw::Status::ResourceExhausted();
	}

	/* Trigger MKBP to notify AP that data is pending in the ring buffer */
	if (wakeUp) {
		mkbp_send_event(EC_MKBP_EVENT_BLUETOOTH);
	}

	return pw::OkStatus();
}

pw::Status
CrosHostcmdTransport::HandleInboundHostCommand(pw::span<const uint8_t> payload)
{
	if (!msg_handler_) {
		LOG_WRN("Not ready to handle AP commands");
		return pw::Status::Unavailable();
	}

	/*
	 * The AP sends data but doesn't wait for a synchronous reply.
	 * We provide an unused callback to satisfy the interface.
	 */
	HostTransport::RespondToHost unused_resp_cb =
		[](pw::Status /*status*/,
		   std::optional<pw::span<const uint8_t> > /*data*/)
		-> pw::Status { return pw::OkStatus(); };

	return msg_handler_(0 /* RAW type */, payload, unused_resp_cb);
}

size_t CrosHostcmdTransport::GetPendingEvents(uint8_t *out_buffer,
					      size_t max_size,
					      uint32_t *num_events)
{
	uint8_t *out_ptr = out_buffer;
	uint32_t events_packed = 0;
	size_t current_total_size = 0;

	k_spinlock_key_t key = k_spin_lock(&rb_lock_);
	while (ring_buf_size_get(&bt_events_ring_buf_) > 0) {
		uint8_t event_len = 0;

		/* Peek length of next event to see if it fits in this Host
		 * Command response */
		if (ring_buf_peek(&bt_events_ring_buf_, &event_len, 1) != 1) {
			break;
		}

		/* Deadlock Protection: Discard events that can never fit in the
		 * MTU */
		if (static_cast<size_t>(event_len + 1) > max_size) {
			ring_buf_consume(
				&bt_events_ring_buf_,
				MIN(static_cast<uint32_t>(event_len + 1),
				    ring_buf_size_get(&bt_events_ring_buf_)));
			continue;
		}

		if ((current_total_size + static_cast<size_t>(event_len) + 1) >
		    max_size) {
			break;
		}

		/* Extract length byte */
		ring_buf_get(&bt_events_ring_buf_, &event_len, 1);
		*out_ptr++ = event_len;

		/* Extract payload */
		ring_buf_get(&bt_events_ring_buf_, out_ptr, event_len);

		out_ptr += event_len;
		current_total_size += (static_cast<size_t>(event_len) + 1);
		events_packed++;
	}
	k_spin_unlock(&rb_lock_, key);

	*num_events = events_packed;
	return current_total_size;
}

} // namespace chre

static enum ec_status hc_bt_command(struct host_cmd_handler_args *args)
{
	const auto *req =
		static_cast<const struct ec_param_bt_command *>(args->params);

	/*
	 * TODO: Implement a reassembly buffer here.
	 * Large messages from the AP will arrive in 240-byte chunks using the
	 * 'offset' and 'total_size' fields (to be added to the struct above).
	 */
	if (req->size == 0 || req->size > BT_MAX_COMMAND_SIZE) {
		return EC_RES_INVALID_PARAM;
	}

	LOG_HEXDUMP_DBG(req->data, req->size, "AP -> HCI Command");

	pw::span<const uint8_t> payload(req->data, req->size);
	pw::Status status =
		chre::CrosHostcmdTransport::HandleInboundHostCommand(payload);

	return status.ok() ? EC_RES_SUCCESS : EC_RES_ERROR;
}
DECLARE_HOST_COMMAND(EC_CMD_BT_COMMAND, hc_bt_command, EC_VER_MASK(0));

static enum ec_status hc_bt_read_event(struct host_cmd_handler_args *args)
{
	auto *rsp =
		static_cast<struct ec_response_bt_read_event *>(args->response);
	uint32_t events_packed = 0;

	size_t total_payload_size =
		chre::CrosHostcmdTransport::GetPendingEvents(
			rsp->events, BT_MAX_EVENT_SIZE, &events_packed);

	if (events_packed == 0) {
		return EC_RES_UNAVAILABLE;
	}

	LOG_DBG("AP polled events: %u items packed (%zu bytes)", events_packed,
		total_payload_size);

	rsp->num_events = events_packed;
	args->response_size = sizeof(rsp->num_events) + total_payload_size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_BT_READ_EVENT, hc_bt_read_event, EC_VER_MASK(0));

static int bt_get_next_event(uint8_t *data)
{
	ARG_UNUSED(data);
	return 0;
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_BLUETOOTH, bt_get_next_event);
