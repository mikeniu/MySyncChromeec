/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>

#if defined(CONFIG_SHELL)
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#endif

extern "C" {
#include "console.h"
#include "ec_commands.h"
#include "uart.h"

#include <zephyr/sys/printk-hooks.h>
}

#include "pw_bytes/span.h"
#include "pw_hdlc/decoder.h"
#include "pw_hdlc/default_addresses.h"
#include "pw_hdlc/encoder.h"
#include "pw_hdlc/rpc_channel.h"
#include "pw_host_command/service.h"
#include "pw_log/log.h"
#include "pw_log_string/handler.h"

#if !defined(CONFIG_ZTEST) && !defined(CONFIG_ARCH_POSIX)
// Provide no-heap stubs for replaceable global allocation/deallocation
// operators and sbrk. In embedded environments without malloc
// (CONFIG_COMMON_LIBC_MALLOC=n), compiler-generated virtual destructors and
// type infos generate references to deleting destructors (operator delete) and
// sbrk. Providing these stubs prevents the linker from pulling in libstdc++'s
// del_op.o / new_op.o and picolibc's picosbrk.c.o (which requires __heap_start
// / __heap_end).
extern "C" {
void *sbrk(intptr_t)
{
	errno = ENOMEM;
	return reinterpret_cast<void *>(-1);
}
void *_sbrk(intptr_t)
{
	errno = ENOMEM;
	return reinterpret_cast<void *>(-1);
}
int __cxa_guard_acquire(uint64_t *guard)
{
	return !*reinterpret_cast<uint8_t *>(guard);
}
void __cxa_guard_release(uint64_t *guard)
{
	*reinterpret_cast<uint8_t *>(guard) = 1;
}
void __cxa_guard_abort(uint64_t *)
{
}
void __cxa_pure_virtual()
{
	while (true) {
	}
}
}

void *operator new(std::size_t) noexcept
{
	return nullptr;
}
void *operator new[](std::size_t) noexcept
{
	return nullptr;
}
void *operator new(std::size_t, const std::nothrow_t &) noexcept
{
	return nullptr;
}
void *operator new[](std::size_t, const std::nothrow_t &) noexcept
{
	return nullptr;
}
void *operator new(std::size_t, std::align_val_t) noexcept
{
	return nullptr;
}
void *operator new[](std::size_t, std::align_val_t) noexcept
{
	return nullptr;
}
void *operator new(std::size_t, std::align_val_t,
		   const std::nothrow_t &) noexcept
{
	return nullptr;
}
void *operator new[](std::size_t, std::align_val_t,
		     const std::nothrow_t &) noexcept
{
	return nullptr;
}

void operator delete(void *) noexcept
{
}
void operator delete[](void *) noexcept
{
}
void operator delete(void *, std::size_t) noexcept
{
}
void operator delete[](void *, std::size_t) noexcept
{
}
void operator delete(void *, std::align_val_t) noexcept
{
}
void operator delete[](void *, std::align_val_t) noexcept
{
}
void operator delete(void *, std::size_t, std::align_val_t) noexcept
{
}
void operator delete[](void *, std::size_t, std::align_val_t) noexcept
{
}
void operator delete(void *, const std::nothrow_t &) noexcept
{
}
void operator delete[](void *, const std::nothrow_t &) noexcept
{
}
void operator delete(void *, std::size_t, const std::nothrow_t &) noexcept
{
}
void operator delete[](void *, std::size_t, const std::nothrow_t &) noexcept
{
}
void operator delete(void *, std::align_val_t, const std::nothrow_t &) noexcept
{
}
void operator delete[](void *, std::align_val_t,
		       const std::nothrow_t &) noexcept
{
}
#endif

namespace chromiumos::ec
{
namespace
{

#if DT_HAS_CHOSEN(zephyr_shell_uart)
#define UART_DEV_NODE DT_CHOSEN(zephyr_shell_uart)
#elif DT_HAS_CHOSEN(zephyr_console)
#define UART_DEV_NODE DT_CHOSEN(zephyr_console)
#endif

#ifdef UART_DEV_NODE
	static const struct device *const uart_dev =
		DEVICE_DT_GET(UART_DEV_NODE);
#else
	static const struct device *const uart_dev = nullptr;
#endif

	static struct k_mutex uart_tx_mutex;

	class ZephyrUartWriter : public pw::stream::NonSeekableWriter {
	    public:
		explicit constexpr ZephyrUartWriter(const struct device *dev)
			: uart_dev_(dev)
		{
		}

	    private:
		pw::Status DoWrite(pw::ConstByteSpan data) override
		{
#ifdef UART_DEV_NODE
			for (std::byte b : data) {
				uart_poll_out(uart_dev_,
					      static_cast<unsigned char>(b));
			}
			return pw::OkStatus();
#else
			return pw::Status::Unimplemented();
#endif
		}

		[[maybe_unused]] const struct device *uart_dev_;
	};

	class ThreadSafeRpcChannelOutput : public pw::rpc::ChannelOutput {
	    public:
		constexpr ThreadSafeRpcChannelOutput(pw::stream::Writer &writer,
						     uint64_t address,
						     const char *channel_name)
			: ChannelOutput(channel_name)
			, writer_(writer)
			, address_(address)
		{
		}

		pw::Status Send(pw::ConstByteSpan buffer) override
		{
			if (!k_is_in_isr()) {
				k_mutex_lock(&uart_tx_mutex, K_FOREVER);
			}
			pw::Status status = pw::hdlc::WriteUIFrame(
				address_, buffer, writer_);
			if (!k_is_in_isr()) {
				k_mutex_unlock(&uart_tx_mutex);
			}
			return status;
		}

	    private:
		pw::stream::Writer &writer_;
		const uint64_t address_;
	};

	HostCommandService host_command_service;
	ZephyrUartWriter uart_writer(uart_dev);
	ThreadSafeRpcChannelOutput hdlc_output(uart_writer,
					       pw::hdlc::kDefaultRpcAddress,
					       "HDLC_UART");

	pw::rpc::Channel rpc_channels[] = {
		pw::rpc::Channel::Create<1>(&hdlc_output),
	};
	pw::rpc::Server rpc_server(rpc_channels);

	extern "C" void pw_log_string_HandleMessageVaList(
		int level, unsigned int flags, const char *module_name,
		const char *file_name, int line_number, const char *message,
		va_list args)
	{
#ifdef UART_DEV_NODE
		if (!device_is_ready(uart_dev)) {
			return;
		}
		char buf[128];
		int len = vsnprintf(buf, sizeof(buf), message, args);
		if (len > 0) {
			if (len < static_cast<int>(sizeof(buf)) - 1 &&
			    buf[len - 1] != '\n') {
				buf[len++] = '\n';
				buf[len] = '\0';
			}
			if (k_is_in_isr()) {
				pw::hdlc::WriteUIFrame(
					pw::hdlc::kDefaultLogAddress,
					pw::as_bytes(pw::span(
						buf, static_cast<size_t>(len))),
					uart_writer);
			} else {
				k_mutex_lock(&uart_tx_mutex, K_FOREVER);
				pw::hdlc::WriteUIFrame(
					pw::hdlc::kDefaultLogAddress,
					pw::as_bytes(pw::span(
						buf, static_cast<size_t>(len))),
					uart_writer);
				k_mutex_unlock(&uart_tx_mutex);
			}
		}
#endif
	}

	static char printk_buf[128];
	static size_t printk_len = 0;
	static bool in_printk_hook = false;

	static int pw_hc_printk_char_hook(int c)
	{
		if (k_is_in_isr() || in_printk_hook) {
			return c;
		}
		k_mutex_lock(&uart_tx_mutex, K_FOREVER);
		in_printk_hook = true;
		if (printk_len < sizeof(printk_buf) - 1) {
			printk_buf[printk_len++] = static_cast<char>(c);
		}
		if (c == '\n' || printk_len >= sizeof(printk_buf) - 1) {
			pw::hdlc::WriteUIFrame(
				pw::hdlc::kDefaultLogAddress,
				pw::as_bytes(pw::span(printk_buf, printk_len)),
				uart_writer);
			printk_len = 0;
		}
		in_printk_hook = false;
		k_mutex_unlock(&uart_tx_mutex);
		return c;
	}

	K_THREAD_STACK_DEFINE(pw_hc_stack, 2048);
	struct k_thread pw_hc_thread_data;

	void pw_hc_uart_server_entry(void *, void *, void *)
	{
#ifdef UART_DEV_NODE
		if (!device_is_ready(uart_dev)) {
			PW_LOG_ERROR(
				"UART device not ready for Pigweed Host Command Server");
			return;
		}

#if defined(CONFIG_PLATFORM_EC_CONSOLE) || defined(CONFIG_SHELL)
		// Ensure Zephyr UART and console have initialized before
		// stopping the shell.
		while (!uart_init_done()) {
			k_msleep(10);
		}
		uart_shell_stop();
		__printk_hook_install(pw_hc_printk_char_hook);
#endif

#else
		PW_LOG_ERROR("No UART device chosen in devicetree");
		return;
#endif

		rpc_server.RegisterService(host_command_service);

		constexpr size_t kDecoderBufferSize =
			pw::hdlc::Decoder::RequiredBufferSizeForFrameSize(
				sizeof(struct ec_host_response) +
				CONFIG_PLATFORM_EC_HOSTCMD_MAX_PAYLOAD_SIZE +
				64);
		std::array<std::byte, kDecoderBufferSize> decode_buffer;
		pw::hdlc::Decoder decoder(decode_buffer);

		while (true) {
			int c;
			bool had_data = false;
			while ((c = uart_getc()) >= 0) {
				had_data = true;
				std::byte byte = static_cast<std::byte>(c);
				if (auto result = decoder.Process(byte);
				    result.ok()) {
					pw::hdlc::Frame &frame = result.value();
					if (frame.address() ==
					    pw::hdlc::kDefaultRpcAddress) {
						rpc_server.ProcessPacket(
							frame.data());
					}
				}
			}

			if (!had_data) {
				k_msleep(1);
			}
		}
	}

	int pw_hc_uart_server_init(void)
	{
		k_mutex_init(&uart_tx_mutex);
		k_thread_create(&pw_hc_thread_data, pw_hc_stack,
				K_THREAD_STACK_SIZEOF(pw_hc_stack),
				pw_hc_uart_server_entry, NULL, NULL, NULL,
				K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
		k_thread_name_set(&pw_hc_thread_data, "pw_hc_uart");
		return 0;
	}

	SYS_INIT(pw_hc_uart_server_init, APPLICATION,
		 CONFIG_APPLICATION_INIT_PRIORITY);

} // namespace
} // namespace chromiumos::ec
