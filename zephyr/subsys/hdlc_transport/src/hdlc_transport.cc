/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hdlc_transport.h"
#include "pw_bytes/span.h"
#include "pw_hdlc/decoder.h"
#include "pw_hdlc/default_addresses.h"
#include "pw_hdlc/encoder.h"
#include "pw_stream/stream.h"

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/ring_buffer.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>

#if DT_HAS_CHOSEN(zephyr_shell_uart)
#define HDLC_UART_NODE DT_CHOSEN(zephyr_shell_uart)
#elif DT_HAS_CHOSEN(zephyr_console)
#define HDLC_UART_NODE DT_CHOSEN(zephyr_console)
#else
#error "No zephyr,shell-uart or zephyr,console node in devicetree"
#endif

static const struct device *const uart_dev = DEVICE_DT_GET(HDLC_UART_NODE);

namespace
{

class UartStreamWriter : public pw::stream::NonSeekableWriter {
    public:
	constexpr UartStreamWriter(const struct device *dev)
		: dev_(dev)
	{
	}

    private:
	pw::Status DoWrite(pw::ConstByteSpan data) override
	{
		for (std::byte b : data) {
			uart_poll_out(dev_, static_cast<unsigned char>(b));
		}
		return pw::OkStatus();
	}

	const struct device *dev_;
};

UartStreamWriter uart_writer(uart_dev);

struct HandlerEntry {
	uint64_t address;
	hdlc_rx_callback_t callback;
	void *user_data;
	bool in_use;
};

constexpr size_t kMaxHandlers = 8;
HandlerEntry handlers[kMaxHandlers];
struct k_spinlock handler_lock;

K_MUTEX_DEFINE(tx_mutex);
volatile bool in_panic = false;

RING_BUF_DECLARE(hdlc_rx_ringbuf, CONFIG_HDLC_TRANSPORT_RX_BUF_SIZE);
K_SEM_DEFINE(hdlc_rx_sem, 0, K_SEM_MAX_LIMIT);
pw::hdlc::DecoderBuffer<CONFIG_HDLC_TRANSPORT_MAX_FRAME_SIZE> decoder;

void dispatch_frame(uint64_t address, const uint8_t *data, size_t len)
{
	hdlc_rx_callback_t cb = nullptr;
	void *ud = nullptr;

	k_spinlock_key_t key = k_spin_lock(&handler_lock);
	for (size_t i = 0; i < kMaxHandlers; ++i) {
		if (handlers[i].in_use && handlers[i].address == address) {
			cb = handlers[i].callback;
			ud = handlers[i].user_data;
			break;
		}
	}
	k_spin_unlock(&handler_lock, key);

	if (cb != nullptr) {
		cb(data, len, ud);
	}
}

void process_byte(uint8_t byte)
{
	auto result = decoder.Process(static_cast<std::byte>(byte));
	if (result.ok()) {
		pw::hdlc::Frame frame = result.value();
		dispatch_frame(
			frame.address(),
			reinterpret_cast<const uint8_t *>(frame.data().data()),
			frame.data().size());
	}
}

void hdlc_rx_thread_fn(void *, void *, void *)
{
	while (true) {
		k_sem_take(&hdlc_rx_sem, K_FOREVER);
		uint8_t byte;
		while (ring_buf_get(&hdlc_rx_ringbuf, &byte, 1) == 1) {
			process_byte(byte);
		}
	}
}

K_THREAD_DEFINE(hdlc_rx_thread_id, CONFIG_HDLC_TRANSPORT_RX_STACK_SIZE,
		hdlc_rx_thread_fn, NULL, NULL, NULL,
		K_PRIO_PREEMPT(CONFIG_HDLC_TRANSPORT_RX_PRIORITY), 0, 0);

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
void hdlc_uart_irq_callback(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);
	uart_irq_update(dev);
	if (uart_irq_rx_ready(dev)) {
		uint8_t buf[32];
		int len;
		while ((len = uart_fifo_read(dev, buf, sizeof(buf))) > 0) {
			ring_buf_put(&hdlc_rx_ringbuf, buf, len);
			k_sem_give(&hdlc_rx_sem);
		}
	}
}
#endif

int hdlc_transport_init(void)
{
	if (!device_is_ready(uart_dev)) {
		return -ENODEV;
	}

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
	uart_irq_callback_user_data_set(uart_dev, hdlc_uart_irq_callback, NULL);
	uart_irq_rx_enable(uart_dev);
#endif

	return 0;
}

SYS_INIT(hdlc_transport_init, POST_KERNEL, CONFIG_HDLC_TRANSPORT_INIT_PRIORITY);

} // namespace

extern "C" void hdlc_set_panic_mode(void)
{
	in_panic = true;
}

extern "C" int hdlc_send_frame(uint64_t address, const void *data, size_t len)
{
	if (!device_is_ready(uart_dev)) {
		return -ENODEV;
	}

	bool lock_acquired = false;
	if (!in_panic && !k_is_in_isr()) {
		k_mutex_lock(&tx_mutex, K_FOREVER);
		lock_acquired = true;
	}

	pw::ConstByteSpan payload(static_cast<const std::byte *>(data), len);
	pw::Status status =
		pw::hdlc::WriteUIFrame(address, payload, uart_writer);

	if (lock_acquired) {
		k_mutex_unlock(&tx_mutex);
	}

	return status.ok() ? 0 : -EIO;
}

extern "C" int hdlc_register_handler(uint64_t address,
				     hdlc_rx_callback_t callback,
				     void *user_data)
{
	k_spinlock_key_t key = k_spin_lock(&handler_lock);
	for (size_t i = 0; i < kMaxHandlers; ++i) {
		if (handlers[i].in_use && handlers[i].address == address) {
			k_spin_unlock(&handler_lock, key);
			return -EEXIST;
		}
	}
	for (size_t i = 0; i < kMaxHandlers; ++i) {
		if (!handlers[i].in_use) {
			handlers[i].address = address;
			handlers[i].callback = callback;
			handlers[i].user_data = user_data;
			handlers[i].in_use = true;
			k_spin_unlock(&handler_lock, key);
			return 0;
		}
	}
	k_spin_unlock(&handler_lock, key);
	return -ENOMEM;
}

extern "C" int hdlc_unregister_handler(uint64_t address)
{
	k_spinlock_key_t key = k_spin_lock(&handler_lock);
	for (size_t i = 0; i < kMaxHandlers; ++i) {
		if (handlers[i].in_use && handlers[i].address == address) {
			handlers[i].in_use = false;
			k_spin_unlock(&handler_lock, key);
			return 0;
		}
	}
	k_spin_unlock(&handler_lock, key);
	return -ENOENT;
}

#ifdef CONFIG_ZTEST
extern "C" void hdlc_test_inject_rx(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; ++i) {
		process_byte(data[i]);
	}
}

extern "C" void hdlc_test_reset_panic(void)
{
	in_panic = false;
}
#endif
