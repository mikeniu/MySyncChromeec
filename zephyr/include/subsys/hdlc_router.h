/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_SUBSYS_HDLC_ROUTER_H_
#define ZEPHYR_INCLUDE_SUBSYS_HDLC_ROUTER_H_

#include "pw_bytes/span.h"
#include "pw_hdlc/decoder.h"
#include "pw_hdlc/default_addresses.h"
#include "pw_hdlc/encoder.h"
#include "pw_result/result.h"
#include "pw_status/status.h"
#include "pw_stream/stream.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace ec::hdlc
{

/**
 * HDLC Channel / Address Identifiers.
 * In HDLC (ISO/IEC 13239, RFC 1662, and Pigweed HDLC), these endpoints are
 * identified by the Address field. Sequential small integers (< 128) are
 * encoded in a single byte over the wire.
 */
namespace Address
{
	// Low nibble (0x01 - 0x0F): Standard core firmware protocols
	inline constexpr uint64_t kTextLog = 0x01; // Plain text console/logs
	inline constexpr uint64_t kTokenizedLog = 0x02; // Binary tokenized logs
							// (pw_log_tokenized)
	inline constexpr uint64_t kHostCommand = 0x03; // ChromeOS EC Host
						       // Commands
	inline constexpr uint64_t kRpc = 0x04; // Pigweed RPC channel (pw_rpc)
	inline constexpr uint64_t kCrashDump = 0x05; // Panicinfo / crash dump /
						     // coredump stream
	inline constexpr uint64_t kConsoleCmd = 0x06; // EC Shell / Console
						      // command execution
	inline constexpr uint64_t kMetrics = 0x07; // Telemetry / tracing / perf
						   // metrics

	inline constexpr uint64_t kStandardMax = 0x0F;

	// High nibble (0x10 - 0xF0): Reserved for board-specific & custom
	// channels
	inline constexpr uint64_t kCustomBase = 0x10;
	inline constexpr uint64_t kCustomMask = 0xF0;

	constexpr bool IsCustom(uint64_t address)
	{
		return (address & kCustomMask) != 0;
	}

	constexpr bool IsStandard(uint64_t address)
	{
		return !IsCustom(address) && address != 0;
	}
} // namespace Address

// Backward-compatibility aliases
inline constexpr uint64_t kConsoleAddress = Address::kTextLog;
inline constexpr uint64_t kTokenizedLogAddress = Address::kTokenizedLog;
inline constexpr uint64_t kHostCommandAddress = Address::kHostCommand;
inline constexpr uint64_t kRpcAddress = Address::kRpc;

/**
 * Callback function signature for receiving frames routed to a specific
 * address.
 *
 * @param frame_data The payload portion of the decoded HDLC frame.
 * @param user_context Opaque pointer passed during route registration.
 * @return pw::OkStatus() on success, or an error status code.
 */
using ChannelHandler = pw::Status (*)(pw::ConstByteSpan frame_data,
				      void *user_context);

struct Route {
	uint64_t address = 0;
	ChannelHandler handler = nullptr;
	void *context = nullptr;

	constexpr Route() = default;
	constexpr Route(uint64_t addr, ChannelHandler h, void *ctx = nullptr)
		: address(addr)
		, handler(h)
		, context(ctx)
	{
	}

	constexpr explicit operator bool() const
	{
		return handler != nullptr;
	}
};

struct RouterStats {
	uint32_t bytes_received = 0;
	uint32_t frames_decoded = 0;
	uint32_t decode_errors = 0;
	uint32_t host_commands_handled = 0;
	uint32_t text_logs_handled = 0;
	uint32_t tokenized_logs_handled = 0;
	uint32_t rpc_frames_handled = 0;
	uint32_t custom_frames_handled = 0;
	uint32_t unhandled_frames = 0;
	uint32_t tx_frames_sent = 0;
	uint32_t tx_errors = 0;
};

/**
 * BufferedWriter collects outgoing bytes into a small chunk buffer before
 * flushing to the underlying Writer, reducing per-byte driver overhead.
 */
class BufferedWriter : public pw::stream::NonSeekableReaderWriter {
    public:
	constexpr BufferedWriter(pw::stream::Writer &writer,
				 pw::ByteSpan buffer)
		: writer_(writer)
		, buffer_(buffer)
	{
	}

	~BufferedWriter() override
	{
		Flush().IgnoreError();
	}

	pw::Status Flush()
	{
		if (buffered_bytes_ == 0) {
			return pw::OkStatus();
		}
		pw::Status status =
			writer_.Write(buffer_.first(buffered_bytes_));
		buffered_bytes_ = 0;
		return status;
	}

    private:
	pw::Status DoWrite(pw::ConstByteSpan data) override
	{
		while (!data.empty()) {
			size_t available = buffer_.size() - buffered_bytes_;
			if (available == 0) {
				pw::Status status = Flush();
				if (!status.ok()) {
					return status;
				}
				available = buffer_.size();
			}
			size_t to_copy = std::min(available, data.size());
			std::memcpy(buffer_.data() + buffered_bytes_,
				    data.data(), to_copy);
			buffered_bytes_ += to_copy;
			data = data.subspan(to_copy);
		}
		return pw::OkStatus();
	}

	pw::StatusWithSize DoRead(pw::ByteSpan) override
	{
		return pw::StatusWithSize::Unimplemented();
	}

	pw::stream::Writer &writer_;
	pw::ByteSpan buffer_;
	size_t buffered_bytes_ = 0;
};

/**
 * Flexible HDLC Router with dynamic channel registration and zero-overhead
 * dispatch.
 */
class HdlcRouter {
    public:
	static constexpr size_t kDefaultMaxRoutes = 8;

	/**
	 * Construct an HdlcRouter.
	 *
	 * @param writer Outgoing stream writer (e.g. UART writer, MemoryWriter)
	 * @param decoder Decoder instance backed by a caller-owned buffer
	 * @param routes_storage User-provided storage span for route table
	 * @param hostcmd_response_buffer Optional buffer for Host Command
	 * responses
	 */
	explicit HdlcRouter(pw::stream::Writer &writer,
			    pw::hdlc::Decoder &decoder,
			    std::span<Route> routes_storage,
			    pw::ByteSpan hostcmd_response_buffer = {})
		: writer_(writer)
		, decoder_(decoder)
		, routes_(routes_storage)
		, hostcmd_response_buffer_(hostcmd_response_buffer)
	{
	}

	// Register or update a channel route
	pw::Status RegisterRoute(uint64_t address, ChannelHandler handler,
				 void *context = nullptr);

	// Unregister a channel route
	pw::Status UnregisterRoute(uint64_t address);

	// Ingest a single byte from the underlying transport
	pw::Status ProcessByte(std::byte byte);

	// Ingest a chunk of bytes from the underlying transport
	pw::Status ProcessBytes(pw::ConstByteSpan bytes);

	// Outgoing frame transmission helpers
	pw::Status SendFrame(uint64_t address, pw::ConstByteSpan payload);
	pw::Status SendHostCommandResponse(pw::ConstByteSpan response);
	pw::Status SendTextLog(pw::ConstByteSpan log);
	pw::Status SendTokenizedLog(pw::ConstByteSpan tokenized_log);
	pw::Status SendRpcPacket(pw::ConstByteSpan rpc_packet);

	// Convenience built-in host command handler registration
	pw::Status EnableHostCommandHandler(pw::ByteSpan response_buffer);

	// Optional fallback handler for unrouted/unknown addresses
	void SetDefaultFallbackHandler(ChannelHandler fallback,
				       void *context = nullptr)
	{
		fallback_handler_ = fallback;
		fallback_context_ = context;
	}

	// Access metrics
	const RouterStats &stats() const
	{
		return stats_;
	}

	uint32_t HostCommandsHandled() const
	{
		return stats_.host_commands_handled;
	}

	uint32_t TextLogsHandled() const
	{
		return stats_.text_logs_handled;
	}

	uint32_t TokenizedLogsHandled() const
	{
		return stats_.tokenized_logs_handled;
	}

	uint32_t RpcFramesHandled() const
	{
		return stats_.rpc_frames_handled;
	}

	void ResetStats()
	{
		stats_ = {};
	}

    private:
	pw::Status DispatchFrame(const pw::hdlc::Frame &frame);
	pw::Status HandleHostCommand(pw::ConstByteSpan request_payload);

	pw::stream::Writer &writer_;
	pw::hdlc::Decoder &decoder_;
	std::span<Route> routes_;
	pw::ByteSpan hostcmd_response_buffer_;

	ChannelHandler fallback_handler_ = nullptr;
	void *fallback_context_ = nullptr;

	RouterStats stats_{};
};

/**
 * Convenient template wrapper that embeds route storage inside the class
 * instance.
 */
template <size_t kMaxRoutes = HdlcRouter::kDefaultMaxRoutes>
class HdlcRouterWithStorage : public HdlcRouter {
    public:
	explicit HdlcRouterWithStorage(pw::stream::Writer &writer,
				       pw::hdlc::Decoder &decoder,
				       pw::ByteSpan hostcmd_response_buffer = {})
		: HdlcRouter(writer, decoder, route_storage_,
			     hostcmd_response_buffer)
	{
	}

    private:
	std::array<Route, kMaxRoutes> route_storage_{};
};

} // namespace ec::hdlc

#endif // ZEPHYR_INCLUDE_SUBSYS_HDLC_ROUTER_H_
