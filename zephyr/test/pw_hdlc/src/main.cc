/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "pw_bytes/span.h"
#include "pw_hdlc/decoder.h"
#include "pw_hdlc/default_addresses.h"
#include "pw_hdlc/encoder.h"
#include "pw_result/result.h"
#include "pw_status/status.h"
#include "pw_stream/memory_stream.h"
#include "subsys/hdlc_router.h"

#include <zephyr/ztest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{

using ec::hdlc::Address::kHostCommand;
using ec::hdlc::Address::kRpc;
using ec::hdlc::Address::kTextLog;
using ec::hdlc::Address::kTokenizedLog;
using ec::hdlc::BufferedWriter;
using ec::hdlc::HdlcRouter;
using ec::hdlc::HdlcRouterWithStorage;
using ec::hdlc::Route;

// Helper to construct a v3 host command packet
std::vector<uint8_t> CreateHostCmdPacket(uint16_t command, uint8_t version,
					 const void *params, size_t params_len)
{
	struct ec_host_request req = {};
	req.struct_version = EC_HOST_REQUEST_VERSION;
	req.checksum = 0;
	req.command = command;
	req.command_version = version;
	req.reserved = 0;
	req.data_len = static_cast<uint16_t>(params_len);

	std::vector<uint8_t> packet(sizeof(req) + params_len);
	std::memcpy(packet.data(), &req, sizeof(req));
	if (params_len > 0 && params != nullptr) {
		std::memcpy(packet.data() + sizeof(req), params, params_len);
	}

	uint8_t csum = 0;
	for (uint8_t b : packet) {
		csum += b;
	}
	auto *hdr = reinterpret_cast<struct ec_host_request *>(packet.data());
	hdr->checksum = static_cast<uint8_t>(-csum);

	return packet;
}

// Helper to decode a single frame from memory buffer
pw::Result<pw::hdlc::Frame> DecodeSingleFrame(pw::hdlc::Decoder &decoder,
					      pw::ConstByteSpan data)
{
	for (std::byte b : data) {
		auto res = decoder.Process(b);
		if (res.ok()) {
			return res;
		}
	}
	return pw::Status::Unavailable();
}

ZTEST(pw_hdlc, test_encode_decode_basic)
{
	std::array<std::byte, 128> encoded_buffer;
	pw::stream::MemoryWriter writer(encoded_buffer);

	const char payload_str[] = "Hello Pigweed HDLC!";
	pw::ConstByteSpan payload =
		pw::as_bytes(pw::span(payload_str, sizeof(payload_str)));

	pw::Status status = pw::hdlc::WriteUIFrame(kRpc, payload, writer);
	zassert_true(status.ok(), "WriteUIFrame failed: %d",
		     static_cast<int>(status.code()));

	pw::ConstByteSpan encoded_data = writer.WrittenData();
	zassert_true(encoded_data.size() > payload.size(),
		     "Encoded data should be larger than payload");

	zassert_equal(encoded_data.front(), std::byte{ 0x7E },
		      "Frame must start with 0x7E flag");
	zassert_equal(encoded_data.back(), std::byte{ 0x7E },
		      "Frame must end with 0x7E flag");

	pw::hdlc::DecoderBuffer<256> decoder;
	auto result = DecodeSingleFrame(decoder, encoded_data);
	zassert_true(result.ok(), "Failed to decode HDLC frame");

	pw::hdlc::Frame frame = result.value();
	zassert_equal(frame.address(), kRpc);
	zassert_equal(frame.data().size(), payload.size());
	zassert_mem_equal(frame.data().data(), payload.data(), payload.size());
}

ZTEST(pw_hdlc, test_encode_decode_escaping)
{
	std::array<std::byte, 128> encoded_buffer;
	pw::stream::MemoryWriter writer(encoded_buffer);

	const std::byte special_payload[] = {
		std::byte{ 0x11 }, std::byte{ 0x7E }, std::byte{ 0x22 },
		std::byte{ 0x7D }, std::byte{ 0x33 }, std::byte{ 0x7E },
	};
	pw::ConstByteSpan payload(special_payload, sizeof(special_payload));

	pw::Status status = pw::hdlc::WriteUIFrame(kTextLog, payload, writer);
	zassert_true(status.ok());

	pw::ConstByteSpan encoded_data = writer.WrittenData();
	zassert_true(encoded_data.size() > payload.size() + 6);

	pw::hdlc::DecoderBuffer<256> decoder;
	auto result = DecodeSingleFrame(decoder, encoded_data);
	zassert_true(result.ok());

	pw::hdlc::Frame frame = result.value();
	zassert_equal(frame.address(), kTextLog);
	zassert_equal(frame.data().size(), sizeof(special_payload));
	zassert_mem_equal(frame.data().data(), special_payload,
			  sizeof(special_payload));
}

ZTEST(pw_hdlc, test_corrupted_fcs_detection)
{
	std::array<std::byte, 128> encoded_buffer;
	pw::stream::MemoryWriter writer(encoded_buffer);

	const char payload_str[] = "Test CRC Corruption";
	pw::ConstByteSpan payload =
		pw::as_bytes(pw::span(payload_str, sizeof(payload_str)));

	pw::Status status = pw::hdlc::WriteUIFrame(kRpc, payload, writer);
	zassert_true(status.ok());

	std::vector<std::byte> corrupted_data(writer.WrittenData().begin(),
					      writer.WrittenData().end());
	zassert_true(corrupted_data.size() > 5);
	corrupted_data[4] = static_cast<std::byte>(
		static_cast<uint8_t>(corrupted_data[4]) ^ 0xFF);

	pw::hdlc::DecoderBuffer<256> decoder;
	pw::Status last_status = pw::Status::Unavailable();

	for (std::byte b : corrupted_data) {
		auto res = decoder.Process(b);
		if (!res.ok() && res.status() != pw::Status::Unavailable()) {
			last_status = res.status();
		}
	}

	zassert_equal(last_status, pw::Status::DataLoss(),
		      "Corrupted FCS must trigger Status::DataLoss()");
}

ZTEST(pw_hdlc, test_router_end_to_end_hostcmd_hello)
{
	std::array<std::byte, 512> outgoing_buffer;
	pw::stream::MemoryWriter outgoing_writer(outgoing_buffer);
	pw::hdlc::DecoderBuffer<512> router_decoder;
	std::array<std::byte, 512> tx_response_buf;

	HdlcRouterWithStorage<4> router(outgoing_writer, router_decoder);
	zassert_true(router.EnableHostCommandHandler(tx_response_buf).ok());

	// 1. Prepare Host Command HELLO request
	struct ec_params_hello params = { .in_data = 0x12345678 };
	auto req_packet =
		CreateHostCmdPacket(EC_CMD_HELLO, 0, &params, sizeof(params));

	// 2. Wrap request in an HDLC frame as sent by host client
	std::array<std::byte, 256> client_tx_buffer;
	pw::stream::MemoryWriter client_tx_writer(client_tx_buffer);
	pw::Status encode_status = pw::hdlc::WriteUIFrame(
		kHostCommand,
		pw::as_bytes(pw::span(req_packet.data(), req_packet.size())),
		client_tx_writer);
	zassert_true(encode_status.ok());

	// 3. Feed the HDLC stream into the router
	pw::Status process_status =
		router.ProcessBytes(client_tx_writer.WrittenData());
	zassert_true(process_status.ok(), "Router failed to process frame: %d",
		     static_cast<int>(process_status.code()));
	zassert_equal(router.HostCommandsHandled(), 1);

	// 4. Verify the router wrote back a response HDLC frame
	pw::ConstByteSpan resp_stream = outgoing_writer.WrittenData();
	zassert_true(resp_stream.size() > sizeof(struct ec_host_response),
		     "Expected response frame in output stream");

	pw::hdlc::DecoderBuffer<512> client_decoder;
	auto client_res = DecodeSingleFrame(client_decoder, resp_stream);
	zassert_true(client_res.ok(), "Failed to decode response HDLC frame");

	pw::hdlc::Frame resp_frame = client_res.value();
	zassert_equal(resp_frame.address(), kHostCommand);

	// 5. Inspect Host Command Response payload
	zassert_true(resp_frame.data().size() >=
		     sizeof(struct ec_host_response) +
			     sizeof(struct ec_response_hello));

	const auto *resp_hdr =
		reinterpret_cast<const struct ec_host_response *>(
			resp_frame.data().data());
	zassert_equal(resp_hdr->struct_version, EC_HOST_RESPONSE_VERSION);
	zassert_equal(resp_hdr->result, EC_RES_SUCCESS,
		      "Host command returned error code %d", resp_hdr->result);
	zassert_equal(resp_hdr->data_len, sizeof(struct ec_response_hello));

	const auto *resp_body =
		reinterpret_cast<const struct ec_response_hello *>(
			resp_frame.data().data() +
			sizeof(struct ec_host_response));
	zassert_equal(
		resp_body->out_data, 0x12345678 + 0x01020304,
		"EC_CMD_HELLO output mismatch (expected 0x%08x, got 0x%08x)",
		0x12345678 + 0x01020304, resp_body->out_data);
}

ZTEST(pw_hdlc, test_router_end_to_end_hostcmd_get_version)
{
	std::array<std::byte, 512> outgoing_buffer;
	pw::stream::MemoryWriter outgoing_writer(outgoing_buffer);
	pw::hdlc::DecoderBuffer<512> router_decoder;
	std::array<std::byte, 512> tx_response_buf;

	HdlcRouterWithStorage<4> router(outgoing_writer, router_decoder);
	zassert_true(router.EnableHostCommandHandler(tx_response_buf).ok());

	// Prepare EC_CMD_GET_VERSION request (no params)
	auto req_packet =
		CreateHostCmdPacket(EC_CMD_GET_VERSION, 0, nullptr, 0);

	std::array<std::byte, 256> client_tx_buffer;
	pw::stream::MemoryWriter client_tx_writer(client_tx_buffer);
	zassert_true(pw::hdlc::WriteUIFrame(
			     kHostCommand,
			     pw::as_bytes(pw::span(req_packet.data(),
						   req_packet.size())),
			     client_tx_writer)
			     .ok());

	zassert_true(router.ProcessBytes(client_tx_writer.WrittenData()).ok());
	zassert_equal(router.HostCommandsHandled(), 1);

	pw::hdlc::DecoderBuffer<512> client_decoder;
	auto client_res = DecodeSingleFrame(client_decoder,
					    outgoing_writer.WrittenData());
	zassert_true(client_res.ok());

	pw::hdlc::Frame resp_frame = client_res.value();
	zassert_equal(resp_frame.address(), kHostCommand);

	const auto *resp_hdr =
		reinterpret_cast<const struct ec_host_response *>(
			resp_frame.data().data());
	zassert_equal(resp_hdr->struct_version, EC_HOST_RESPONSE_VERSION);
	zassert_equal(resp_hdr->result, EC_RES_SUCCESS);
	zassert_equal(resp_hdr->data_len,
		      sizeof(struct ec_response_get_version));
}

ZTEST(pw_hdlc, test_router_multiplexing_interleaved_with_tokenized_logs)
{
	std::array<std::byte, 2048> outgoing_buffer;
	pw::stream::MemoryWriter outgoing_writer(outgoing_buffer);
	pw::hdlc::DecoderBuffer<512> router_decoder;
	std::array<std::byte, 512> tx_response_buf;

	std::vector<std::string> received_text_logs;
	std::vector<std::vector<uint8_t> > received_tokenized_logs;
	std::vector<std::vector<uint8_t> > received_rpc_messages;

	auto text_log_cb = [](pw::ConstByteSpan data, void *ctx) -> pw::Status {
		auto *list = static_cast<std::vector<std::string> *>(ctx);
		list->emplace_back(reinterpret_cast<const char *>(data.data()),
				   data.size());
		return pw::OkStatus();
	};

	auto tokenized_log_cb = [](pw::ConstByteSpan data,
				   void *ctx) -> pw::Status {
		auto *list =
			static_cast<std::vector<std::vector<uint8_t> > *>(ctx);
		list->emplace_back(
			reinterpret_cast<const uint8_t *>(data.data()),
			reinterpret_cast<const uint8_t *>(data.data()) +
				data.size());
		return pw::OkStatus();
	};

	auto rpc_cb = [](pw::ConstByteSpan data, void *ctx) -> pw::Status {
		auto *list =
			static_cast<std::vector<std::vector<uint8_t> > *>(ctx);
		list->emplace_back(
			reinterpret_cast<const uint8_t *>(data.data()),
			reinterpret_cast<const uint8_t *>(data.data()) +
				data.size());
		return pw::OkStatus();
	};

	HdlcRouterWithStorage<8> router(outgoing_writer, router_decoder);
	zassert_true(router.EnableHostCommandHandler(tx_response_buf).ok());
	zassert_true(
		router.RegisterRoute(kTextLog, text_log_cb, &received_text_logs)
			.ok());
	zassert_true(router.RegisterRoute(kTokenizedLog, tokenized_log_cb,
					  &received_tokenized_logs)
			     .ok());
	zassert_true(
		router.RegisterRoute(kRpc, rpc_cb, &received_rpc_messages).ok());

	// Build composite multiplexed stream
	std::array<std::byte, 1024> input_stream_buffer;
	pw::stream::MemoryWriter input_writer(input_stream_buffer);

	// 1. Text console log (Address 0x01)
	const char log1[] = "Console log message line 1\n";
	pw::hdlc::WriteUIFrame(kTextLog,
			       pw::as_bytes(pw::span(log1, sizeof(log1) - 1)),
			       input_writer);

	// 2. Tokenized log binary token (Address 0x02)
	const uint8_t tokenized_data[] = { 0x4a, 0x2b, 0x1c, 0x00, 0x05, 0x10 };
	pw::hdlc::WriteUIFrame(
		kTokenizedLog,
		pw::as_bytes(pw::span(tokenized_data, sizeof(tokenized_data))),
		input_writer);

	// 3. Host Command (Address 0x03)
	struct ec_params_hello p1 = { .in_data = 0x11111111 };
	auto req1 = CreateHostCmdPacket(EC_CMD_HELLO, 0, &p1, sizeof(p1));
	pw::hdlc::WriteUIFrame(kHostCommand,
			       pw::as_bytes(pw::span(req1.data(), req1.size())),
			       input_writer);

	// 4. RPC Frame (Address 0x04)
	const uint8_t rpc_payload[] = { 0x08, 0x01, 0x12, 0x04,
					0xde, 0xad, 0xbe, 0xef };
	pw::hdlc::WriteUIFrame(
		kRpc, pw::as_bytes(pw::span(rpc_payload, sizeof(rpc_payload))),
		input_writer);

	// Process entire multiplexed stream
	pw::Status status = router.ProcessBytes(input_writer.WrittenData());
	zassert_true(status.ok());

	zassert_equal(router.TextLogsHandled(), 1);
	zassert_equal(router.TokenizedLogsHandled(), 1);
	zassert_equal(router.HostCommandsHandled(), 1);
	zassert_equal(router.RpcFramesHandled(), 1);

	// Verify text log
	zassert_equal(received_text_logs.size(), 1);
	zassert_equal(received_text_logs[0], "Console log message line 1\n");

	// Verify tokenized log
	zassert_equal(received_tokenized_logs.size(), 1);
	zassert_mem_equal(received_tokenized_logs[0].data(), tokenized_data,
			  sizeof(tokenized_data));

	// Verify RPC message
	zassert_equal(received_rpc_messages.size(), 1);
	zassert_mem_equal(received_rpc_messages[0].data(), rpc_payload,
			  sizeof(rpc_payload));
}

ZTEST(pw_hdlc, test_router_dynamic_routes_and_fallback)
{
	std::array<std::byte, 512> outgoing_buffer;
	pw::stream::MemoryWriter outgoing_writer(outgoing_buffer);
	pw::hdlc::DecoderBuffer<256> router_decoder;

	HdlcRouterWithStorage<2> router(outgoing_writer, router_decoder);

	uint32_t custom_route_hits = 0;
	auto custom_cb = [](pw::ConstByteSpan data, void *ctx) -> pw::Status {
		(*static_cast<uint32_t *>(ctx))++;
		return pw::OkStatus();
	};

	zassert_true(ec::hdlc::Address::IsStandard(kTextLog));
	zassert_true(ec::hdlc::Address::IsStandard(kTokenizedLog));
	zassert_true(ec::hdlc::Address::IsStandard(kHostCommand));
	zassert_true(ec::hdlc::Address::IsStandard(kRpc));
	zassert_false(ec::hdlc::Address::IsCustom(kTextLog));

	zassert_true(ec::hdlc::Address::IsCustom(0x10));
	zassert_true(ec::hdlc::Address::IsCustom(0x20));
	zassert_false(ec::hdlc::Address::IsStandard(0x10));

	// Register custom route 0x10
	zassert_true(
		router.RegisterRoute(0x10, custom_cb, &custom_route_hits).ok());

	// Fallback handler for unrouted packets
	uint32_t fallback_hits = 0;
	auto fallback_cb = [](pw::ConstByteSpan data, void *ctx) -> pw::Status {
		(*static_cast<uint32_t *>(ctx))++;
		return pw::OkStatus();
	};
	router.SetDefaultFallbackHandler(fallback_cb, &fallback_hits);

	// Frame on registered route 0x10
	std::array<std::byte, 64> tx_buf;
	pw::stream::MemoryWriter tx_writer(tx_buf);
	const char d1[] = "route 0x10 data";
	pw::hdlc::WriteUIFrame(0x10, pw::as_bytes(pw::span(d1, sizeof(d1))),
			       tx_writer);

	// Frame on unregistered route 0x20
	const char d2[] = "route 0x20 unhandled";
	pw::hdlc::WriteUIFrame(0x20, pw::as_bytes(pw::span(d2, sizeof(d2))),
			       tx_writer);

	zassert_true(router.ProcessBytes(tx_writer.WrittenData()).ok());
	zassert_equal(custom_route_hits, 1);
	zassert_equal(fallback_hits, 1);

	// Unregister route 0x10
	zassert_true(router.UnregisterRoute(0x10).ok());
	tx_writer.clear();
	pw::hdlc::WriteUIFrame(0x10, pw::as_bytes(pw::span(d1, sizeof(d1))),
			       tx_writer);
	zassert_true(router.ProcessBytes(tx_writer.WrittenData()).ok());
	zassert_equal(custom_route_hits, 1); // no new hit
	zassert_equal(fallback_hits, 2); // routed to fallback
}

ZTEST(pw_hdlc, test_router_send_helpers_all_channels)
{
	std::array<std::byte, 512> outgoing_buffer;
	pw::stream::MemoryWriter outgoing_writer(outgoing_buffer);
	pw::hdlc::DecoderBuffer<256> router_decoder;

	HdlcRouterWithStorage<4> router(outgoing_writer, router_decoder);

	// Test SendTextLog (Address 0x01)
	const char log_msg[] = "Text log message";
	zassert_true(router.SendTextLog(pw::as_bytes(pw::span(log_msg,
							      sizeof(log_msg))))
			     .ok());

	// Test SendTokenizedLog (Address 0x02)
	const uint8_t token_data[] = { 0x12, 0x34, 0x56, 0x78 };
	zassert_true(router
			     .SendTokenizedLog(pw::as_bytes(
				     pw::span(token_data, sizeof(token_data))))
			     .ok());

	// Test SendHostCommandResponse (Address 0x03)
	const uint8_t hc_resp[] = { 0x00, 0x00, 0x00, 0x00 };
	zassert_true(
		router.SendHostCommandResponse(
			      pw::as_bytes(pw::span(hc_resp, sizeof(hc_resp))))
			.ok());

	// Test SendRpcPacket (Address 0x04)
	const uint8_t rpc_data[] = { 0xAA, 0xBB };
	zassert_true(router.SendRpcPacket(pw::as_bytes(pw::span(
						  rpc_data, sizeof(rpc_data))))
			     .ok());

	zassert_equal(router.stats().tx_frames_sent, 4);

	// Decode all 4 outgoing frames and verify addresses
	pw::hdlc::DecoderBuffer<256> client_decoder;
	std::vector<pw::hdlc::Frame> decoded_frames;
	for (std::byte b : outgoing_writer.WrittenData()) {
		auto res = client_decoder.Process(b);
		if (res.ok()) {
			decoded_frames.push_back(res.value());
		}
	}

	zassert_equal(decoded_frames.size(), 4);
	zassert_equal(decoded_frames[0].address(), kTextLog);
	zassert_equal(decoded_frames[1].address(), kTokenizedLog);
	zassert_equal(decoded_frames[2].address(), kHostCommand);
	zassert_equal(decoded_frames[3].address(), kRpc);
}

ZTEST(pw_hdlc, test_buffered_writer)
{
	std::array<std::byte, 256> backing_buffer;
	pw::stream::MemoryWriter backing_writer(backing_buffer);

	std::array<std::byte, 16> chunk_buffer;
	BufferedWriter buffered_writer(backing_writer, chunk_buffer);

	const char test_data[] = "1234567890abcdefghijklmnopqrstuvwxyz";
	pw::ConstByteSpan data_span =
		pw::as_bytes(pw::span(test_data, sizeof(test_data) - 1));

	pw::Status status = pw::hdlc::WriteUIFrame(kHostCommand, data_span,
						   buffered_writer);
	zassert_true(status.ok());

	zassert_true(buffered_writer.Flush().ok());

	pw::hdlc::DecoderBuffer<256> decoder;
	auto res = DecodeSingleFrame(decoder, backing_writer.WrittenData());
	zassert_true(res.ok());
	zassert_equal(res.value().address(), kHostCommand);
	zassert_equal(res.value().data().size(), data_span.size());
	zassert_mem_equal(res.value().data().data(), data_span.data(),
			  data_span.size());
}

} // namespace

ZTEST_SUITE(pw_hdlc, NULL, NULL, NULL, NULL, NULL);
