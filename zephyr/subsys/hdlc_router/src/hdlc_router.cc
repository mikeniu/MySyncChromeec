/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "subsys/hdlc_router.h"

#if defined(CONFIG_PLATFORM_EC_HOSTCMD)
struct host_cmd_handler_args {
	void (*send_response)(struct host_cmd_handler_args *args);
	uint16_t command;
	uint8_t version;
	const void *params;
	uint16_t params_size;
	void *response;
	uint16_t response_max;
	uint16_t response_size;
#ifndef CONFIG_EC_HOST_CMD
	uint16_t result;
#endif
};

extern "C" uint16_t host_command_process(struct host_cmd_handler_args *args);
#elif defined(CONFIG_EC_HOST_CMD)
extern "C" {
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>
}
#endif

namespace ec::hdlc
{

pw::Status HdlcRouter::RegisterRoute(uint64_t address, ChannelHandler handler,
				     void *context)
{
	if (handler == nullptr) {
		return pw::Status::InvalidArgument();
	}

	// First look for existing route to update
	for (auto &route : routes_) {
		if (route.handler != nullptr && route.address == address) {
			route.handler = handler;
			route.context = context;
			return pw::OkStatus();
		}
	}

	// Look for empty slot
	for (auto &route : routes_) {
		if (route.handler == nullptr) {
			route.address = address;
			route.handler = handler;
			route.context = context;
			return pw::OkStatus();
		}
	}

	return pw::Status::ResourceExhausted();
}

pw::Status HdlcRouter::UnregisterRoute(uint64_t address)
{
	for (auto &route : routes_) {
		if (route.handler != nullptr && route.address == address) {
			route = Route{};
			return pw::OkStatus();
		}
	}
	return pw::Status::NotFound();
}

pw::Status HdlcRouter::EnableHostCommandHandler(pw::ByteSpan response_buffer)
{
	hostcmd_response_buffer_ = response_buffer;
	return RegisterRoute(
		Address::kHostCommand,
		[](pw::ConstByteSpan data, void *ctx) -> pw::Status {
			auto *self = static_cast<HdlcRouter *>(ctx);
			return self->HandleHostCommand(data);
		},
		this);
}

pw::Status HdlcRouter::ProcessByte(std::byte byte)
{
	stats_.bytes_received++;

	pw::Result<pw::hdlc::Frame> result = decoder_.Process(byte);
	if (!result.ok()) {
		if (result.status() == pw::Status::Unavailable()) {
			return pw::OkStatus();
		}
		stats_.decode_errors++;
		return result.status();
	}

	stats_.frames_decoded++;
	return DispatchFrame(result.value());
}

pw::Status HdlcRouter::ProcessBytes(pw::ConstByteSpan bytes)
{
	pw::Status last_error = pw::OkStatus();
	for (std::byte b : bytes) {
		pw::Status status = ProcessByte(b);
		if (!status.ok()) {
			last_error = status;
		}
	}
	return last_error;
}

pw::Status HdlcRouter::SendFrame(uint64_t address, pw::ConstByteSpan payload)
{
	pw::Status status = pw::hdlc::WriteUIFrame(address, payload, writer_);
	if (status.ok()) {
		stats_.tx_frames_sent++;
	} else {
		stats_.tx_errors++;
	}
	return status;
}

pw::Status HdlcRouter::SendHostCommandResponse(pw::ConstByteSpan response)
{
	return SendFrame(Address::kHostCommand, response);
}

pw::Status HdlcRouter::SendTextLog(pw::ConstByteSpan log)
{
	return SendFrame(Address::kTextLog, log);
}

pw::Status HdlcRouter::SendTokenizedLog(pw::ConstByteSpan tokenized_log)
{
	return SendFrame(Address::kTokenizedLog, tokenized_log);
}

pw::Status HdlcRouter::SendRpcPacket(pw::ConstByteSpan rpc_packet)
{
	return SendFrame(Address::kRpc, rpc_packet);
}

pw::Status HdlcRouter::DispatchFrame(const pw::hdlc::Frame &frame)
{
	// Check registered routes
	for (const auto &route : routes_) {
		if (route.handler != nullptr &&
		    route.address == frame.address()) {
			// Update metrics
			switch (frame.address()) {
			case Address::kHostCommand:
				stats_.host_commands_handled++;
				break;
			case Address::kTextLog:
				stats_.text_logs_handled++;
				break;
			case Address::kTokenizedLog:
				stats_.tokenized_logs_handled++;
				break;
			case Address::kRpc:
				stats_.rpc_frames_handled++;
				break;
			default:
				stats_.custom_frames_handled++;
				break;
			}
			return route.handler(frame.data(), route.context);
		}
	}

	// No registered route: try default fallback
	if (fallback_handler_ != nullptr) {
		return fallback_handler_(frame.data(), fallback_context_);
	}

	stats_.unhandled_frames++;
	return pw::Status::Unimplemented();
}

pw::Status HdlcRouter::HandleHostCommand(pw::ConstByteSpan request_payload)
{
	if (hostcmd_response_buffer_.empty()) {
		return pw::Status::FailedPrecondition();
	}

	size_t tx_len = 0;

#if defined(CONFIG_PLATFORM_EC_HOSTCMD)
	struct ec_host_request_v3 {
		uint8_t struct_version;
		uint8_t checksum;
		uint16_t command;
		uint8_t command_version;
		uint8_t reserved;
		uint16_t data_len;
	} __attribute__((packed));

	struct ec_host_response_v3 {
		uint8_t struct_version;
		uint8_t checksum;
		uint16_t result;
		uint16_t data_len;
		uint16_t reserved;
	} __attribute__((packed));

	if (request_payload.size() < sizeof(ec_host_request_v3)) {
		return pw::Status::InvalidArgument();
	}
	const auto *req = reinterpret_cast<const ec_host_request_v3 *>(
		request_payload.data());
	if (req->struct_version != 3) {
		return pw::Status::InvalidArgument();
	}
	if (request_payload.size() <
	    sizeof(ec_host_request_v3) + req->data_len) {
		return pw::Status::InvalidArgument();
	}

	uint8_t csum = 0;
	for (size_t i = 0; i < sizeof(ec_host_request_v3) + req->data_len;
	     ++i) {
		csum += static_cast<uint8_t>(request_payload[i]);
	}
	if (csum != 0) {
		return pw::Status::DataLoss();
	}

	if (hostcmd_response_buffer_.size() < sizeof(ec_host_response_v3)) {
		return pw::Status::ResourceExhausted();
	}

	auto *resp = reinterpret_cast<ec_host_response_v3 *>(
		hostcmd_response_buffer_.data());
	uint8_t *resp_data = reinterpret_cast<uint8_t *>(resp + 1);
	size_t resp_max =
		hostcmd_response_buffer_.size() - sizeof(ec_host_response_v3);

	struct host_cmd_handler_args args = {};
	args.command = req->command;
	args.version = req->command_version;
	args.params = req + 1;
	args.params_size = req->data_len;
	args.response = resp_data;
	args.response_max = resp_max;
	args.response_size = 0;

	uint16_t result = host_command_process(&args);

	resp->struct_version = 3;
	resp->result = result;
	resp->data_len = static_cast<uint16_t>(args.response_size);
	resp->reserved = 0;

	tx_len = sizeof(ec_host_response_v3) + resp->data_len;
	uint8_t resp_csum = 0;
	resp->checksum = 0;
	const auto *resp_bytes = reinterpret_cast<const uint8_t *>(resp);
	for (size_t i = 0; i < tx_len; ++i) {
		resp_csum += resp_bytes[i];
	}
	resp->checksum = static_cast<uint8_t>(-resp_csum);

#elif defined(CONFIG_EC_HOST_CMD)
	ec_host_cmd_process_packet(
		reinterpret_cast<const uint8_t *>(request_payload.data()),
		request_payload.size(),
		reinterpret_cast<uint8_t *>(hostcmd_response_buffer_.data()),
		hostcmd_response_buffer_.size(), &tx_len);
#else
	return pw::Status::Unimplemented();
#endif

	if (tx_len == 0) {
		return pw::Status::Internal();
	}

	return SendHostCommandResponse(hostcmd_response_buffer_.first(tx_len));
}

} // namespace ec::hdlc
