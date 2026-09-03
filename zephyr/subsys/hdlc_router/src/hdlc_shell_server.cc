/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "pw_bytes/span.h"
#include "pw_hdlc/decoder.h"
#include "pw_hdlc/default_addresses.h"
#include "pw_hdlc/encoder.h"
#include "pw_result/result.h"
#include "pw_status/status.h"
#include "pw_stream/memory_stream.h"
#include "subsys/hdlc_router.h"

#include <zephyr/kernel.h>
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
#include <zephyr/sys/printk-hooks.h>

void uart_write_char(char c);
void z_shell_write(const struct shell *sh, const void *data, size_t length);
}

namespace
{

using ec::hdlc::Address::kConsoleCmd;
using ec::hdlc::Address::kCustomBase;
using ec::hdlc::Address::kHostCommand;
using ec::hdlc::Address::kRpc;
using ec::hdlc::Address::kTextLog;
using ec::hdlc::Address::kTokenizedLog;
using ec::hdlc::HdlcRouter;
using ec::hdlc::HdlcRouterWithStorage;

// Protocol v3 packet definitions for self-test
struct ec_host_request_header {
	uint8_t struct_version;
	uint8_t checksum;
	uint16_t command;
	uint8_t command_version;
	uint8_t reserved;
	uint16_t data_len;
} __attribute__((packed));

struct ec_host_response_header {
	uint8_t struct_version;
	uint8_t checksum;
	uint16_t result;
	uint16_t data_len;
	uint16_t reserved;
} __attribute__((packed));

struct ec_params_hello_data {
	uint32_t in_data;
} __attribute__((packed));

struct ec_response_hello_data {
	uint32_t out_data;
} __attribute__((packed));

static constexpr uint16_t kEcCmdHello = 0x0001;
static constexpr uint8_t kEcHostRequestVersion = 3;
static constexpr uint16_t kEcResSuccess = 0;

K_MUTEX_DEFINE(g_hdlc_tx_mutex);

class ZephyrUartWriter : public pw::stream::NonSeekableReaderWriter {
    public:
	constexpr ZephyrUartWriter() = default;

    private:
	pw::Status DoWrite(pw::ConstByteSpan data) override
	{
		bool locked = false;
		if (!k_is_in_isr()) {
			k_mutex_lock(&g_hdlc_tx_mutex, K_FOREVER);
			locked = true;
		}

		for (std::byte b : data) {
			uart_write_char(static_cast<char>(b));
		}

		if (locked) {
			k_mutex_unlock(&g_hdlc_tx_mutex);
		}
		return pw::OkStatus();
	}

	pw::StatusWithSize DoRead(pw::ByteSpan) override
	{
		return pw::StatusWithSize::Unimplemented();
	}
};

static bool g_hdlc_server_active = false;
static printk_hook_fn_t g_orig_printk_hook = nullptr;
static std::array<char, 128> g_printk_buf;
static size_t g_printk_buf_len = 0;

static ZephyrUartWriter g_uart_writer;
static std::array<std::byte, 128> g_tx_chunk_buf;
static ec::hdlc::BufferedWriter g_buffered_writer(g_uart_writer,
						  g_tx_chunk_buf);
static std::array<std::byte, 512> g_decoder_buf;
static pw::hdlc::Decoder g_decoder(g_decoder_buf);
static std::array<std::byte, 512> g_hc_resp_buf;
static HdlcRouterWithStorage<8> g_router(g_buffered_writer, g_decoder);

static int hdlc_printk_char_hook(int c)
{
	if (!g_hdlc_server_active) {
		if (g_orig_printk_hook != nullptr) {
			return g_orig_printk_hook(c);
		}
		return c;
	}

	if (c == '\n' || g_printk_buf_len >= g_printk_buf.size() - 1) {
		if (c == '\n') {
			g_printk_buf[g_printk_buf_len++] = '\n';
		}
		g_router.SendTextLog(pw::as_bytes(
			pw::span(g_printk_buf.data(), g_printk_buf_len)));
		g_buffered_writer.Flush().IgnoreError();
		g_printk_buf_len = 0;
	} else if (c != '\r') {
		g_printk_buf[g_printk_buf_len++] = static_cast<char>(c);
	}
	return c;
}

static void hdlc_server_bypass_cb(const struct shell *sh, uint8_t *data,
				  size_t len, void *user_data)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(user_data);
	for (size_t i = 0; i < len; ++i) {
		uint8_t b = data[i];
		g_router.ProcessByte(static_cast<std::byte>(b));
	}
	g_buffered_writer.Flush().IgnoreError();
}

static void stop_hdlc_server(const struct shell *sh)
{
	if (!g_hdlc_server_active) {
		return;
	}

	if (sh != nullptr) {
		shell_set_bypass(sh, NULL, NULL);
	}

	if (g_printk_buf_len > 0) {
		g_router.SendTextLog(pw::as_bytes(
			pw::span(g_printk_buf.data(), g_printk_buf_len)));
		g_buffered_writer.Flush().IgnoreError();
		g_printk_buf_len = 0;
	}

	__printk_hook_install(g_orig_printk_hook);
	g_orig_printk_hook = nullptr;
	g_hdlc_server_active = false;
}

static int command_hdlc_server(const struct shell *sh, size_t argc, char **argv)
{
	const struct shell *uart_sh = shell_backend_uart_get_ptr();
	if (uart_sh == nullptr) {
		shell_error(sh, "No UART shell backend available");
		return -ENODEV;
	}

	if (argc > 1 && strcmp(argv[1], "stop") == 0) {
		stop_hdlc_server(uart_sh);
		shell_print(sh, "[HDLC Server] Stopped.");
		return 0;
	}

	static bool initialized = false;
	if (!initialized) {
		g_router.EnableHostCommandHandler(g_hc_resp_buf);
		g_router.RegisterRoute(
			kTextLog,
			[](pw::ConstByteSpan data, void *ctx) -> pw::Status {
				auto *r = static_cast<HdlcRouter *>(ctx);
				char ack_msg[128] = "ACK text log: ";
				size_t prefix_len = strlen(ack_msg);
				size_t copy_len = std::min(
					data.size(),
					sizeof(ack_msg) - prefix_len - 1);
				std::memcpy(ack_msg + prefix_len, data.data(),
					    copy_len);
				size_t total_len = prefix_len + copy_len;
				ack_msg[total_len] = '\0';
				return r->SendTextLog(pw::as_bytes(
					pw::span(ack_msg, total_len)));
			},
			&g_router);

		g_router.RegisterRoute(
			kTokenizedLog,
			[](pw::ConstByteSpan data, void *ctx) -> pw::Status {
				auto *r = static_cast<HdlcRouter *>(ctx);
				return r->SendTokenizedLog(data);
			},
			&g_router);

		g_router.RegisterRoute(
			kConsoleCmd,
			[](pw::ConstByteSpan data, void *ctx) -> pw::Status {
				auto *r = static_cast<HdlcRouter *>(ctx);
				char cmd_buf[128];
				size_t copy_len = std::min(data.size(),
							   sizeof(cmd_buf) - 1);
				std::memcpy(cmd_buf, data.data(), copy_len);
				cmd_buf[copy_len] = '\0';

				const struct shell *sh_ptr =
					shell_backend_uart_get_ptr();
				int32_t ret = -1;
				if (sh_ptr != nullptr) {
					ret = shell_execute_cmd(sh_ptr,
								cmd_buf);
				}
				return r->SendFrame(
					kConsoleCmd,
					pw::as_bytes(pw::span(
						reinterpret_cast<
							const std::byte *>(&ret),
						sizeof(ret))));
			},
			&g_router);

		g_router.RegisterRoute(
			kCustomBase,
			[](pw::ConstByteSpan data, void *ctx) -> pw::Status {
				ARG_UNUSED(data);
				ARG_UNUSED(ctx);
				const struct shell *sh_ptr =
					shell_backend_uart_get_ptr();
				stop_hdlc_server(sh_ptr);
				return pw::OkStatus();
			},
			nullptr);
		initialized = true;
	}

	g_hdlc_server_active = true;
	g_orig_printk_hook = __printk_get_hook();
	__printk_hook_install(hdlc_printk_char_hook);

	shell_print(
		sh,
		"[HDLC Server] Entering HDLC mode. Send HDLC frames or stop "
		"command to exit.");
	shell_set_bypass(uart_sh, hdlc_server_bypass_cb, NULL);

	return 0;
}

SHELL_CMD_ARG_REGISTER(hdlc_server, NULL,
		       "Run live HDLC server on UART shell: hdlc_server [stop]",
		       command_hdlc_server, 1, 1);

// Test helper: decode single frame from memory
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

static bool g_poc_text_log_received = false;
static bool g_poc_tokenized_log_received = false;

static pw::Status OnPocTextLogFrame(pw::ConstByteSpan data, void *ctx)
{
	ARG_UNUSED(ctx);
	g_poc_text_log_received = true;
	return pw::OkStatus();
}

static pw::Status OnPocTokenizedLogFrame(pw::ConstByteSpan data, void *ctx)
{
	ARG_UNUSED(ctx);
	g_poc_tokenized_log_received = true;
	return pw::OkStatus();
}

static int command_hdlc_poc(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(
		sh,
		"[HDLC POC] Starting flexible HdlcRouter on-device self-test...");

	// 1. Build EC_CMD_HELLO request
	struct ec_params_hello_data params = { .in_data = 0x12345678 };
	struct ec_host_request_header req = {};
	req.struct_version = kEcHostRequestVersion;
	req.checksum = 0;
	req.command = kEcCmdHello;
	req.command_version = 0;
	req.reserved = 0;
	req.data_len = static_cast<uint16_t>(sizeof(params));

	static std::array<uint8_t, sizeof(req) + sizeof(params)> hc_packet;
	std::memcpy(hc_packet.data(), &req, sizeof(req));
	std::memcpy(hc_packet.data() + sizeof(req), &params, sizeof(params));

	uint8_t csum = 0;
	for (uint8_t b : hc_packet) {
		csum += b;
	}
	auto *hdr = reinterpret_cast<struct ec_host_request_header *>(
		hc_packet.data());
	hdr->checksum = static_cast<uint8_t>(-csum);

	// 2. Encode to HDLC client frame (Host Command address 0x03)
	static std::array<std::byte, 256> client_tx_buf;
	pw::stream::MemoryWriter client_tx_writer(client_tx_buf);
	pw::Status st = pw::hdlc::WriteUIFrame(
		kHostCommand,
		pw::as_bytes(pw::span(hc_packet.data(), hc_packet.size())),
		client_tx_writer);
	if (!st.ok()) {
		shell_error(sh, "[HDLC POC] FAIL: Client encode failed (%d)",
			    static_cast<int>(st.code()));
		return -EIO;
	}

	// 3. Router setup with dynamic route table
	static std::array<std::byte, 512> router_output_buf;
	pw::stream::MemoryWriter router_output_writer(router_output_buf);
	static std::array<std::byte, 512> router_decoder_buf;
	pw::hdlc::Decoder router_decoder(router_decoder_buf);
	static std::array<std::byte, 512> tx_response_buf;

	HdlcRouterWithStorage<4> router(router_output_writer, router_decoder);
	router.EnableHostCommandHandler(tx_response_buf);
	router.RegisterRoute(kTextLog, OnPocTextLogFrame, nullptr);
	router.RegisterRoute(kTokenizedLog, OnPocTokenizedLogFrame, nullptr);

	g_poc_text_log_received = false;
	g_poc_tokenized_log_received = false;

	// 4. Feed HDLC packet to Router
	st = router.ProcessBytes(client_tx_writer.WrittenData());
	if (!st.ok()) {
		shell_error(sh,
			    "[HDLC POC] FAIL: Router ProcessBytes failed (%d)",
			    static_cast<int>(st.code()));
		return -EIO;
	}

	// 5. Decode response from Router
	static std::array<std::byte, 512> client_decoder_buf;
	pw::hdlc::Decoder client_decoder(client_decoder_buf);
	auto res = DecodeSingleFrame(client_decoder,
				     router_output_writer.WrittenData());
	if (!res.ok()) {
		shell_error(sh,
			    "[HDLC POC] FAIL: Failed to decode response frame");
		return -EIO;
	}

	pw::hdlc::Frame resp_frame = res.value();
	if (resp_frame.address() != kHostCommand) {
		shell_error(sh, "[HDLC POC] FAIL: Wrong response address: 0x%x",
			    static_cast<unsigned int>(resp_frame.address()));
		return -EINVAL;
	}

	const auto *resp_hdr =
		reinterpret_cast<const struct ec_host_response_header *>(
			resp_frame.data().data());
	if (resp_hdr->result != kEcResSuccess) {
		shell_error(sh,
			    "[HDLC POC] FAIL: Host command returned error %d",
			    resp_hdr->result);
		return -EIO;
	}

	const auto *resp_body =
		reinterpret_cast<const struct ec_response_hello_data *>(
			resp_frame.data().data() +
			sizeof(struct ec_host_response_header));
	if (resp_body->out_data != 0x12345678 + 0x01020304) {
		shell_error(sh,
			    "[HDLC POC] FAIL: HELLO output mismatch: 0x%08x",
			    resp_body->out_data);
		return -EINVAL;
	}

	// 6. Test Text Log channel (Address 0x01)
	const char test_log[] = "Hello from HDLC Text Log!";
	static std::array<std::byte, 128> text_tx_buf;
	pw::stream::MemoryWriter text_tx_writer(text_tx_buf);
	pw::hdlc::WriteUIFrame(
		kTextLog, pw::as_bytes(pw::span(test_log, sizeof(test_log))),
		text_tx_writer);

	router.ProcessBytes(text_tx_writer.WrittenData());
	if (!g_poc_text_log_received) {
		shell_error(sh,
			    "[HDLC POC] FAIL: Text log callback not invoked");
		return -EIO;
	}

	// 7. Test Tokenized Log channel (Address 0x02)
	const uint8_t tokenized_payload[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0x01 };
	static std::array<std::byte, 64> tok_tx_buf;
	pw::stream::MemoryWriter tok_tx_writer(tok_tx_buf);
	pw::hdlc::WriteUIFrame(
		kTokenizedLog,
		pw::as_bytes(
			pw::span(tokenized_payload, sizeof(tokenized_payload))),
		tok_tx_writer);

	router.ProcessBytes(tok_tx_writer.WrittenData());
	if (!g_poc_tokenized_log_received) {
		shell_error(
			sh,
			"[HDLC POC] FAIL: Tokenized log callback not invoked");
		return -EIO;
	}

	shell_print(
		sh,
		"[HDLC POC] PASS: All channels (HostCmd=0x03, TextLog=0x01, "
		"TokenizedLog=0x02) verified!");
	return 0;
}

SHELL_CMD_REGISTER(hdlcpoc, NULL, "Run Pigweed HDLC self-test",
		   command_hdlc_poc);

} // namespace
