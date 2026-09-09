/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hdlc_transport.h"
#include "pw_bytes/span.h"
#include "pw_hdlc/decoder.h"
#include "pw_hdlc/default_addresses.h"
#include "pw_hdlc/encoder.h"
#include "pw_stream/memory_stream.h"

#include <zephyr/device.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include <array>
#include <cstring>
#include <vector>

struct hdlc_transport_fixture {
	const struct device *dev;
};

static void *setup(void)
{
	static struct hdlc_transport_fixture fixture;
	fixture.dev = DEVICE_DT_GET(DT_NODELABEL(euart0));
	zassert_not_null(fixture.dev);
	zassert_true(device_is_ready(fixture.dev));
	return &fixture;
}

static void before(void *f)
{
	auto *fixture = static_cast<struct hdlc_transport_fixture *>(f);
	uart_emul_flush_tx_data(fixture->dev);
	uart_emul_flush_rx_data(fixture->dev);
	hdlc_test_reset_panic();
}

ZTEST_SUITE(hdlc_transport, NULL, setup, before, NULL, NULL);

ZTEST_F(hdlc_transport, test_handler_registration)
{
	auto noop_cb = [](const uint8_t *, size_t, void *) {};

	/* Registering handler for test address succeeds */
	zassert_equal(0, hdlc_register_handler(0x10, noop_cb, nullptr));

	/* Registering again for the same address returns -EEXIST */
	zassert_equal(-EEXIST, hdlc_register_handler(0x10, noop_cb, nullptr));

	/* Unregister succeeds */
	zassert_equal(0, hdlc_unregister_handler(0x10));

	/* Unregistering again returns -ENOENT */
	zassert_equal(-ENOENT, hdlc_unregister_handler(0x10));
}

ZTEST_F(hdlc_transport, test_handler_table_capacity)
{
	auto noop_cb = [](const uint8_t *, size_t, void *) {};

	std::vector<uint64_t> registered;
	for (uint64_t addr = 0x100; addr < 0x110; ++addr) {
		int ret = hdlc_register_handler(addr, noop_cb, nullptr);
		if (ret == 0) {
			registered.push_back(addr);
		} else {
			zassert_equal(-ENOMEM, ret);
			break;
		}
	}

	for (uint64_t addr : registered) {
		zassert_equal(0, hdlc_unregister_handler(addr));
	}
}

ZTEST_F(hdlc_transport, test_send_frame_log)
{
	const char *msg = "Test log frame payload";
	size_t msg_len = strlen(msg);

	int ret = hdlc_send_frame(HDLC_ADDR_LOG, msg, msg_len);
	zassert_equal(0, ret);

	uint8_t tx_buf[256];
	uint32_t tx_bytes =
		uart_emul_get_tx_data(fixture->dev, tx_buf, sizeof(tx_buf));
	zassert_true(tx_bytes > 0,
		     "Expected bytes transmitted to emulated UART");

	pw::hdlc::DecoderBuffer<256> dec;
	bool frame_found = false;
	for (uint32_t i = 0; i < tx_bytes; ++i) {
		auto res = dec.Process(static_cast<std::byte>(tx_buf[i]));
		if (res.ok()) {
			pw::hdlc::Frame frame = res.value();
			zassert_equal(HDLC_ADDR_LOG, frame.address());
			zassert_equal(msg_len, frame.data().size());
			zassert_mem_equal(frame.data().data(), msg, msg_len);
			frame_found = true;
			break;
		}
	}
	zassert_true(frame_found,
		     "Expected valid HDLC frame decoded from TX output");
}

ZTEST_F(hdlc_transport, test_send_frame_shell)
{
	const char *msg = "shell echo response";
	size_t msg_len = strlen(msg);

	int ret = hdlc_send_frame(HDLC_ADDR_SHELL, msg, msg_len);
	zassert_equal(0, ret);

	uint8_t tx_buf[256];
	uint32_t tx_bytes =
		uart_emul_get_tx_data(fixture->dev, tx_buf, sizeof(tx_buf));
	zassert_true(tx_bytes > 0);

	pw::hdlc::DecoderBuffer<256> dec;
	bool frame_found = false;
	for (uint32_t i = 0; i < tx_bytes; ++i) {
		auto res = dec.Process(static_cast<std::byte>(tx_buf[i]));
		if (res.ok()) {
			pw::hdlc::Frame frame = res.value();
			zassert_equal(HDLC_ADDR_SHELL, frame.address());
			zassert_equal(msg_len, frame.data().size());
			zassert_mem_equal(frame.data().data(), msg, msg_len);
			frame_found = true;
			break;
		}
	}
	zassert_true(frame_found);
}

struct RxContext {
	bool called = false;
	std::vector<uint8_t> payload;
};

static void test_rx_cb(const uint8_t *data, size_t len, void *user_data)
{
	auto *ctx = static_cast<RxContext *>(user_data);
	ctx->called = true;
	ctx->payload.assign(data, data + len);
}

ZTEST_F(hdlc_transport, test_receive_frame_dispatch)
{
	RxContext ctx;
	constexpr uint64_t kTestAddr = 0x42;
	zassert_equal(0, hdlc_register_handler(kTestAddr, test_rx_cb, &ctx));

	std::array<std::byte, 128> enc_buf;
	pw::stream::MemoryWriter writer(enc_buf);
	const char *payload_str = "hello_rx";
	pw::ConstByteSpan payload(
		reinterpret_cast<const std::byte *>(payload_str),
		strlen(payload_str));
	zassert_true(pw::hdlc::WriteUIFrame(kTestAddr, payload, writer).ok());

	pw::ConstByteSpan encoded = writer.WrittenData();
	hdlc_test_inject_rx(reinterpret_cast<const uint8_t *>(encoded.data()),
			    encoded.size());

	zassert_true(ctx.called, "Expected RX callback invoked");
	zassert_equal(strlen(payload_str), ctx.payload.size());
	zassert_mem_equal(ctx.payload.data(), payload_str, strlen(payload_str));

	zassert_equal(0, hdlc_unregister_handler(kTestAddr));
}

ZTEST_F(hdlc_transport, test_corrupt_frame_rejected)
{
	RxContext ctx;
	constexpr uint64_t kTestAddr = 0x43;
	zassert_equal(0, hdlc_register_handler(kTestAddr, test_rx_cb, &ctx));

	std::array<std::byte, 128> enc_buf;
	pw::stream::MemoryWriter writer(enc_buf);
	const char *payload_str = "corrupt_data";
	pw::ConstByteSpan payload(
		reinterpret_cast<const std::byte *>(payload_str),
		strlen(payload_str));
	zassert_true(pw::hdlc::WriteUIFrame(kTestAddr, payload, writer).ok());

	/* Corrupt a byte in the frame */
	enc_buf[3] =
		static_cast<std::byte>(static_cast<uint8_t>(enc_buf[3]) ^ 0xFF);

	pw::ConstByteSpan encoded = writer.WrittenData();
	hdlc_test_inject_rx(reinterpret_cast<const uint8_t *>(encoded.data()),
			    encoded.size());

	zassert_false(ctx.called, "Corrupted frame must not trigger callback");

	zassert_equal(0, hdlc_unregister_handler(kTestAddr));
}

ZTEST_F(hdlc_transport, test_unregistered_address_ignored)
{
	RxContext ctx;
	constexpr uint64_t kTestAddr = 0x44;
	zassert_equal(0, hdlc_register_handler(kTestAddr, test_rx_cb, &ctx));

	/* Send frame to a different, unregistered address (0x45) */
	std::array<std::byte, 128> enc_buf;
	pw::stream::MemoryWriter writer(enc_buf);
	const char *payload_str = "unregistered";
	pw::ConstByteSpan payload(
		reinterpret_cast<const std::byte *>(payload_str),
		strlen(payload_str));
	zassert_true(pw::hdlc::WriteUIFrame(0x45, payload, writer).ok());

	pw::ConstByteSpan encoded = writer.WrittenData();
	hdlc_test_inject_rx(reinterpret_cast<const uint8_t *>(encoded.data()),
			    encoded.size());

	zassert_false(ctx.called,
		      "Callback must not be invoked for unhandled address");

	zassert_equal(0, hdlc_unregister_handler(kTestAddr));
}

ZTEST_F(hdlc_transport, test_panic_mode)
{
	hdlc_set_panic_mode();

	const char *msg = "panic log message";
	size_t msg_len = strlen(msg);
	int ret = hdlc_send_frame(HDLC_ADDR_LOG, msg, msg_len);
	zassert_equal(0, ret);

	uint8_t tx_buf[256];
	uint32_t tx_bytes =
		uart_emul_get_tx_data(fixture->dev, tx_buf, sizeof(tx_buf));
	zassert_true(tx_bytes > 0);

	pw::hdlc::DecoderBuffer<256> dec;
	bool frame_found = false;
	for (uint32_t i = 0; i < tx_bytes; ++i) {
		auto res = dec.Process(static_cast<std::byte>(tx_buf[i]));
		if (res.ok()) {
			pw::hdlc::Frame frame = res.value();
			zassert_equal(HDLC_ADDR_LOG, frame.address());
			zassert_equal(msg_len, frame.data().size());
			frame_found = true;
			break;
		}
	}
	zassert_true(frame_found);
	hdlc_test_reset_panic();
}

ZTEST_F(hdlc_transport, test_shell_hdlc_rx_dispatch)
{
	/* Inject a shell keystroke 'help\r' framed to HDLC_ADDR_SHELL */
	std::array<std::byte, 128> enc_buf;
	pw::stream::MemoryWriter writer(enc_buf);
	const char *cmd = "help\r";
	pw::ConstByteSpan payload(reinterpret_cast<const std::byte *>(cmd),
				  strlen(cmd));
	zassert_true(
		pw::hdlc::WriteUIFrame(HDLC_ADDR_SHELL, payload, writer).ok());

	pw::ConstByteSpan encoded = writer.WrittenData();
	hdlc_test_inject_rx(reinterpret_cast<const uint8_t *>(encoded.data()),
			    encoded.size());

	/* Sleep briefly to let shell thread process the command */
	k_sleep(K_MSEC(100));

	uint8_t tx_buf[1024];
	uint32_t tx_bytes =
		uart_emul_get_tx_data(fixture->dev, tx_buf, sizeof(tx_buf));
	zassert_true(tx_bytes > 0, "Shell should have responded to command");

	/* Decode frames - verify at least one is on HDLC_ADDR_SHELL */
	pw::hdlc::DecoderBuffer<1024> dec;
	bool found_shell_frame = false;
	for (uint32_t i = 0; i < tx_bytes; ++i) {
		auto res = dec.Process(static_cast<std::byte>(tx_buf[i]));
		if (res.ok()) {
			pw::hdlc::Frame frame = res.value();
			if (frame.address() == HDLC_ADDR_SHELL) {
				found_shell_frame = true;
			}
		}
	}
	zassert_true(found_shell_frame,
		     "Expected shell output framed to HDLC_ADDR_SHELL");
}

ZTEST_F(hdlc_transport, test_restore_irq)
{
	/* Calling restore_irq re-attaches the ISR and enables RX */
	hdlc_transport_restore_irq();

	/* Verify RX still works after restore_irq */
	RxContext ctx;
	constexpr uint64_t kTestAddr = 0x46;
	zassert_equal(0, hdlc_register_handler(kTestAddr, test_rx_cb, &ctx));

	std::array<std::byte, 128> enc_buf;
	pw::stream::MemoryWriter writer(enc_buf);
	const char *payload_str = "after_restore";
	pw::ConstByteSpan payload(
		reinterpret_cast<const std::byte *>(payload_str),
		strlen(payload_str));
	zassert_true(pw::hdlc::WriteUIFrame(kTestAddr, payload, writer).ok());

	pw::ConstByteSpan encoded = writer.WrittenData();
	hdlc_test_inject_rx(reinterpret_cast<const uint8_t *>(encoded.data()),
			    encoded.size());

	zassert_true(ctx.called);
	zassert_equal(strlen(payload_str), ctx.payload.size());
	zassert_mem_equal(ctx.payload.data(), payload_str, strlen(payload_str));

	zassert_equal(0, hdlc_unregister_handler(kTestAddr));
}

ZTEST_F(hdlc_transport, test_tx_busy_state)
{
	/* Verify tx_busy state control and dropped frame counter */
	hdlc_test_set_tx_busy(false);
	uint32_t drops = hdlc_test_get_isr_dropped_frames();
	zassert_equal(0, drops);
}
