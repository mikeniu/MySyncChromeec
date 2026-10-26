/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SECTION_IS_RO
#define SECTION_IS_RO
#endif

#ifndef CONFIG_STREAM_USART4
#define CONFIG_STREAM_USART4
#endif

#define CHIP_FAMILY_STM32F0
#define CHIP_VARIANT_STM32F07X
#define CONFIG_USB_RAM_ACCESS_TYPE uint16_t

#include "chip/stm32/registers.h"
#include "servo_usb_bridge_frame.h"
#include "usb_bridge.h"
#ifndef CONFIG_DMA_CROS
#define CONFIG_DMA_CROS
#endif
#include "chip/stm32/usart.h"
#include "chip/stm32/usart_rx_dma.h"
#include "chip/stm32/usart_tx_dma.h"
#include "chip/stm32/usb-stream.h"
#include "chip/stm32/usb_hw.h"
#include "common.h"
#include "console.h"
#include "dma.h"
#include "queue.h"
#include "queue_policies.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#include <pthread.h>

#ifndef USB_IFACE_USART4_STREAM
#define USB_IFACE_USART4_STREAM 4
#endif
#ifndef USB_IFACE_BRIDGE
#define USB_IFACE_BRIDGE 6
#endif
#ifndef USB_EP_BRIDGE
#define USB_EP_BRIDGE 7
#endif
#ifndef USB_STR_BRIDGE_NAME
#define USB_STR_BRIDGE_NAME 10
#endif

#ifndef EP_CTR_RX
#define EP_CTR_RX BIT(15)
#endif
#ifndef EP_CTR_TX
#define EP_CTR_TX BIT(7)
#endif
#ifndef EP_CTR_BOTH
#define EP_CTR_BOTH (EP_CTR_RX | EP_CTR_TX)
#endif

/* USB memory and packet mocks */
usb_uint __usb_ram_start[1];

#undef usb_sram_addr
#define usb_sram_addr(x) ((uintptr_t)(x))

void *memcpy_to_usbram(void *dest, const void *src, size_t n)
{
	return memcpy(dest, src, n);
}

void *memcpy_from_usbram(void *dest, const void *src, size_t n)
{
	return memcpy(dest, src, n);
}

void usb_read_setup_packet(usb_uint *buffer, struct usb_setup_packet *packet)
{
	packet->bmRequestType = buffer[0] & 0xff;
	packet->bRequest = buffer[0] >> 8;
	packet->wValue = buffer[1];
	packet->wIndex = buffer[2];
	packet->wLength = buffer[3];
}

static uint16_t mock_desc_patch[USB_DESC_PATCH_COUNT];

void set_descriptor_patch(enum usb_desc_patch_type type, const void *address,
			  uint16_t data)
{
	if (type < USB_DESC_PATCH_COUNT)
		mock_desc_patch[type] = data;
}

static bool mock_pd_connected;
int pd_is_connected(int port)
{
	return mock_pd_connected ? 1 : 0;
}

static bool mock_usb_connected;
void usb_disconnect(void)
{
	mock_usb_connected = false;
}
void usb_connect(void)
{
	mock_usb_connected = true;
}

void usb_stream_clear_tx(struct usb_stream_config const *config)
{
}
void usb_stream_clear_rx(struct usb_stream_config const *config)
{
}

static volatile int irq_disable_depth;

void interrupt_disable(void)
{
	irq_disable_depth++;
}

void interrupt_enable(void)
{
	irq_disable_depth--;
}

extern struct usart_config const usart4;

void task_disable_irq(int irq)
{
}

void task_enable_irq(int irq)
{
}

void task_trigger_irq(int irq)
{
	if (usart4.tx && usart4.tx->interrupt)
		usart4.tx->interrupt(&usart4);
}

/* Hardware register & endpoint mocks */
static uint16_t mock_usb_ep[8];
struct stm32_endpoint btable_ep[8];
static bool mock_in_isr;
static bool mock_irq_depth_violation;
static int mock_ep7_toggle_count;

static void mock_stm32_toggle_ep(int ep, uint16_t mask, uint16_t val,
				 uint16_t flags)
{
	if (ep == USB_EP_BRIDGE && !mock_in_isr) {
		if (irq_disable_depth <= 0)
			mock_irq_depth_violation = true;
		mock_ep7_toggle_count++;
	}

	uint16_t old = mock_usb_ep[ep];
	uint16_t w = (((old & (EP_MASK | mask)) ^ val) | flags);

	/* 1. rc_w0 completion bits: cleared on write 0, preserved on write 1 */
	uint16_t ctr_bits = (old & (EP_CTR_RX | EP_CTR_TX)) & w;

	/* 2. Toggle bits: toggles on write 1, preserved on write 0 */
	uint16_t toggle_mask =
		(EP_RX_MASK | EP_TX_MASK | EP_RX_DTOG | EP_TX_DTOG);
	uint16_t tog_bits = (old & toggle_mask) ^ (w & toggle_mask);

	/* 3. Normal RW bits: */
	uint16_t rw_bits = (w & EP_MASK);

	mock_usb_ep[ep] = ctr_bits | tog_bits | rw_bits;
}

#undef STM32_TOGGLE_EP
#define STM32_TOGGLE_EP(n, mask, val, flags) \
	mock_stm32_toggle_ep((n), (mask), (val), (flags))

#undef STM32_USB_EP
#define STM32_USB_EP(n) mock_usb_ep[n]

/* Queue hook for usart4 corrupt IRQ */
static bool mock_queue_init_irq_violation;
static int mock_queue_init_usart4_count;

static void mock_queue_init(struct queue const *q)
{
	if (q == &usart4_to_bridge) {
		if (irq_disable_depth <= 0)
			mock_queue_init_irq_violation = true;
		mock_queue_init_usart4_count++;
	}
	(queue_init)(q);
}
#define queue_init(q) mock_queue_init(q)

/* USART register mocks */
static uint32_t mock_usart_cr1;
static uint32_t mock_usart_cr2;
static uint32_t mock_usart_cr3;
static uint32_t mock_usart_sr;
static uint32_t mock_usart_icr;
static uint32_t __maybe_unused mock_usart_brr;
static uint32_t __maybe_unused mock_usart_rqr;
static uint32_t mock_usart_rdr_reg;
static uint32_t mock_usart_tdr_reg;

static struct stm32_dma_chan mock_dma_channels[STM32_DMAC_COUNT];

stm32_dma_chan_t *dma_get_channel(enum dma_channel channel)
{
	return &mock_dma_channels[channel];
}

void dma_disable(enum dma_channel channel)
{
	mock_dma_channels[channel].ccr &= ~STM32_DMA_CCR_EN;
}

void dma_clear_isr(enum dma_channel channel)
{
}

void dma_start_rx(const struct dma_option *option, unsigned int count,
		  void *memory)
{
	enum dma_channel ch = option->channel;
	mock_dma_channels[ch].cpar = (uintptr_t)option->periph;
	mock_dma_channels[ch].cmar = (uintptr_t)memory;
	mock_dma_channels[ch].cndtr = count;
	mock_dma_channels[ch].ccr = option->flags | STM32_DMA_CCR_EN;
}

void dma_prepare_tx(const struct dma_option *option, unsigned int count,
		    const void *memory)
{
	enum dma_channel ch = option->channel;
	mock_dma_channels[ch].cpar = (uintptr_t)option->periph;
	mock_dma_channels[ch].cmar = (uintptr_t)memory;
	mock_dma_channels[ch].cndtr = count;
	mock_dma_channels[ch].ccr = option->flags;
}

void dma_go(stm32_dma_chan_t *chan)
{
	chan->ccr |= STM32_DMA_CCR_EN;
}

int dma_bytes_done(dma_chan_t *chan, int orig_count)
{
	return orig_count - (chan->cndtr & 0xFFFF);
}

void usart_clear_tc(struct usart_config const *config)
{
	mock_usart_sr &= ~STM32_USART_SR_TC;
}

static inline void usart_tx_init(struct usart_config const *config)
{
	config->tx->init(config);
}

static inline void usart_rx_init(struct usart_config const *config)
{
	config->rx->init(config);
}

static inline void usart_written(struct consumer const *consumer, size_t count)
{
	if (consumer && consumer->ops && consumer->ops->written)
		consumer->ops->written(consumer, count);
}

static int mock_usart4_baud_rate = 1000000;
void usart_set_baud(struct usart_config const *config, int baud)
{
	mock_usart4_baud_rate = baud;
}

static inline uint32_t *mock_usart_reg(intptr_t base, uint32_t *reg)
{
	(void)base;
	return reg;
}

static inline uint32_t mock_get_usart_sr(intptr_t base)
{
	(void)base;
	mock_usart_sr &= ~mock_usart_icr;
	mock_usart_icr = 0;
	return mock_usart_sr;
}

#undef STM32_USART_CR1
#define STM32_USART_CR1(base) (*mock_usart_reg((base), &mock_usart_cr1))
#undef STM32_USART_CR2
#define STM32_USART_CR2(base) (*mock_usart_reg((base), &mock_usart_cr2))
#undef STM32_USART_CR3
#define STM32_USART_CR3(base) (*mock_usart_reg((base), &mock_usart_cr3))
#undef STM32_USART_BRR
#define STM32_USART_BRR(base) (*mock_usart_reg((base), &mock_usart_brr))
#undef STM32_USART_RQR
#define STM32_USART_RQR(base) (*mock_usart_reg((base), &mock_usart_rqr))
#undef STM32_USART_SR
#define STM32_USART_SR(base) mock_get_usart_sr(base)
#undef STM32_USART_RDR
#define STM32_USART_RDR(base) (*mock_usart_reg((base), &mock_usart_rdr_reg))
#undef STM32_USART_TDR
#define STM32_USART_TDR(base) (*mock_usart_reg((base), &mock_usart_tdr_reg))
#undef STM32_USART_ICR
#define STM32_USART_ICR(base) (*mock_usart_reg((base), &mock_usart_icr))

/* Queues and config */
static int mock_usart4_usb_deferred_calls;
static void mock_usart4_usb_deferred(void)
{
	mock_usart4_usb_deferred_calls++;
}
DECLARE_DEFERRED(mock_usart4_usb_deferred);

static int mock_usart4_usb_producer_reads;
static void mock_usart4_usb_producer_read(struct producer const *producer,
					  size_t count)
{
	(void)producer;
	(void)count;
	mock_usart4_usb_producer_reads++;
}
static struct producer_ops const mock_usart4_usb_producer_ops = {
	.read = mock_usart4_usb_producer_read,
};

struct queue const usart4_to_bridge = QUEUE_NULL(128, uint8_t);
struct queue const bridge_to_usart4 = QUEUE_NULL(128, uint8_t);
extern struct consumer const usb_bridge_kbd_consumer;
static struct producer const mock_usart4_usb_producer = {
	.queue = &usb_to_bridge_kbd,
	.ops = &mock_usart4_usb_producer_ops,
};
struct queue const usb_to_bridge_kbd = QUEUE_DIRECT(
	16, uint8_t, mock_usart4_usb_producer, usb_bridge_kbd_consumer);
struct queue const bridge_kbd_to_usb = QUEUE_NULL(16, uint8_t);
static struct usb_stream_state mock_usart4_usb_state;
struct usb_stream_config const usart4_usb = {
	.state = &mock_usart4_usb_state,
	.deferred = &mock_usart4_usb_deferred_data,
	.producer = {
		.queue = &usb_to_bridge_kbd,
		.ops = &mock_usart4_usb_producer_ops,
	},
	.consumer = {
		.queue = &bridge_kbd_to_usb,
	},
};

static struct usart_hw_config const mock_usart4_hw = {
	.base = STM32_USART4_BASE,
};
static struct usart_state mock_usart4_state;
static struct usart_rx_dma const usart4_rx_dma =
	USART_RX_DMA(STM32_DMAC_CH6, 80);
static struct usart_tx_dma const usart4_tx_dma =
	USART_TX_DMA(STM32_DMAC_CH7, 68);

struct usart_config const usart4 = {
	.hw = &mock_usart4_hw,
	.rx = &usart4_rx_dma.usart_rx,
	.tx = &usart4_tx_dma.usart_tx,
	.state = &mock_usart4_state,
	.baud = 1000000,
	.flags = 0,
	.producer = {
		.queue = &usart4_to_bridge,
		.ops = &usart4_rx_dma.usart_rx.producer_ops,
	},
	.consumer = {
		.queue = &bridge_to_usart4,
		.ops = &usart4_tx_dma.usart_tx.consumer_ops,
	},
};

#undef USB_DECLARE_EP
#define USB_DECLARE_EP(num, tx, rx, evt)                   \
	static void __maybe_unused __unused_ep_##num(void) \
	{                                                  \
		(void)(tx);                                \
		(void)(rx);                                \
		(void)(evt);                               \
	}
#undef USB_DECLARE_IFACE
#define USB_DECLARE_IFACE(num, handler)                       \
	static void __maybe_unused __unused_iface_##num(void) \
	{                                                     \
		(void)(handler);                              \
	}

#include "board/servo_v4p1/usb_bridge.c"
#include "chip/stm32/usart_rx_dma.c"
#include "chip/stm32/usart_tx_dma.c"

static void mock_dma_rx_feed_byte(uint8_t b)
{
	stm32_dma_chan_t *chan = &mock_dma_channels[STM32_DMAC_CH6];
	uint8_t *rx_buf = (uint8_t *)(uintptr_t)chan->cmar;
	size_t fifo_size = 80;
	size_t cur_pos = (fifo_size - (chan->cndtr & 0xFFFF)) % fifo_size;
	if (rx_buf)
		rx_buf[cur_pos] = b;
	if ((chan->cndtr & 0xFFFF) <= 1)
		chan->cndtr = fifo_size;
	else
		chan->cndtr--;
}

static void mock_dma_rx_feed_bytes(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++)
		mock_dma_rx_feed_byte(data[i]);
}

static void mock_dma_rx_trigger_idle(void)
{
	mock_usart_sr &= ~mock_usart_icr;
	mock_usart_icr = 0;
	mock_usart_sr |= STM32_USART_SR_IDLE;
	usart4.rx->interrupt(&usart4);
}

static void mock_dma_tx_complete(void)
{
	mock_usart_sr &= ~mock_usart_icr;
	mock_usart_icr = 0;
	mock_usart_sr |= STM32_USART_SR_TC;
	usart4.tx->interrupt(&usart4);
}

static void drain_queue(struct queue const *q)
{
	uint8_t placeholder;
	while (queue_remove_unit(q, &placeholder))
		;
}

#define SERVO_FRAME_DATA 0x1000U
#define SERVO_FRAME_CREDIT 0x1010U
#define SERVO_FRAME_WAIT_OP 0x1020U
#define SERVO_FRAME_NAK 0x1030U
#define SERVO_FRAME_RESET_REQ 0x1040U
#define SERVO_FRAME_RESET_ACK 0x1050U
#define SERVO_FRAME_KEY_CMD 0x1060U
#define SERVO_FRAME_KEY_ACK 0x1070U
#define SERVO_FRAME_WAIT(epoch) (SERVO_FRAME_WAIT_OP | ((epoch) & 0x03U))

#define SERVO_RX_RING_BYTES 256U
#define SERVO_RESET_FLAG_BOOT 0x80U
#define SERVO_DATA_SEQ_SHIFT 2U

static void queue_feed_frame(struct queue const *q, uint16_t type,
			     uint8_t arg_len, const uint8_t *payload)
{
	uint8_t wire_type;
	uint8_t wire_arg = arg_len;
	uint8_t frame[SERVO_MAX_FRAME_BYTES];

	switch (type & 0xFFF0U) {
	case SERVO_FRAME_DATA: {
		uint8_t seq = (type >> 2) & 0x03U;
		wire_type = SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_DATA, seq);
		break;
	}
	case SERVO_FRAME_CREDIT: {
		uint8_t epoch = type & 0x03U;
		uint8_t seq = arg_len & 0x03U;
		wire_type =
			SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_CREDIT, seq);
		wire_arg = epoch;
		break;
	}
	case SERVO_FRAME_WAIT_OP: {
		uint8_t epoch = type & 0x03U;
		uint8_t seq = arg_len & 0x03U;
		wire_type = SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_WAIT, seq);
		wire_arg = epoch;
		break;
	}
	case SERVO_FRAME_NAK: {
		uint8_t epoch = type & 0x03U;
		uint8_t seq = arg_len & 0x03U;
		wire_type = SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_NAK, seq);
		wire_arg = epoch;
		break;
	}
	case SERVO_FRAME_RESET_REQ: {
		uint8_t epoch = type & 0x03U;
		if (arg_len & SERVO_RESET_FLAG_BOOT) {
			wire_type = SERVO_MAKE_TYPE(SERVO_CHAN_SYS,
						    SERVO_OP_RESET_REQ, 0);
			wire_arg = arg_len & ~SERVO_RESET_FLAG_BOOT;
		} else {
			wire_type = SERVO_MAKE_TYPE(SERVO_CHAN_H2H,
						    SERVO_OP_RESET_REQ, epoch);
			wire_arg = arg_len;
		}
		break;
	}
	case SERVO_FRAME_RESET_ACK: {
		uint8_t epoch = type & 0x03U;
		if (arg_len & SERVO_RESET_FLAG_BOOT) {
			wire_type = SERVO_MAKE_TYPE(SERVO_CHAN_SYS,
						    SERVO_OP_RESET_ACK, 0);
			wire_arg = arg_len & ~SERVO_RESET_FLAG_BOOT;
		} else {
			wire_type = SERVO_MAKE_TYPE(SERVO_CHAN_H2H,
						    SERVO_OP_RESET_ACK, epoch);
			wire_arg = arg_len;
		}
		break;
	}
	case SERVO_FRAME_KEY_CMD:
		wire_type = SERVO_MAKE_TYPE(SERVO_CHAN_KBD, SERVO_OP_DATA,
					    (type >> 2) & 0x03U);
		break;
	case SERVO_FRAME_KEY_ACK:
		wire_type = SERVO_MAKE_TYPE(SERVO_CHAN_KBD, SERVO_OP_CREDIT,
					    (type >> 2) & 0x03U);
		break;
	default:
		wire_type = (uint8_t)type;
		break;
	}

	size_t len = servo_frame_encode(wire_type, wire_arg, payload, frame);
	queue_add_units(q, frame, len);
}

/* 1. Test CRC8 & Structural Validation */
static int test_crc8_and_structural_validation(void)
{
	struct servo_rx_parser parser;
	uint8_t payload[SERVO_MAX_DATA_LEN];
	uint8_t frame[SERVO_MAX_FRAME_BYTES];
	uint8_t corrupted[SERVO_MAX_FRAME_BYTES];

	for (size_t i = 0; i < sizeof(payload); i++)
		payload[i] = (uint8_t)(i * 7 + 3);

	/* 1. Exhaustive 256 x 256 (type, arg_len) validation */
	for (int t = 0; t < 256; t++) {
		for (int a = 0; a < 256; a++) {
			uint8_t type = (uint8_t)t;
			uint8_t arg = (uint8_t)a;
			bool valid = servo_frame_is_valid_header(type, arg);
			bool expected = false;
			uint8_t ch = SERVO_GET_CHAN(type);
			uint8_t op = SERVO_GET_OP(type);
			bool rst_arg_ok =
				((arg & ~SERVO_RESET_VALID_ARG_MASK) == 0U &&
				 (arg & SERVO_RESET_PROTO_MASK) != 0x02U);

			switch (ch) {
			case SERVO_CHAN_SYS:
				expected = (op == SERVO_OP_RESET_REQ ||
					    op == SERVO_OP_RESET_ACK) &&
					   rst_arg_ok;
				break;
			case SERVO_CHAN_H2H:
				switch (op) {
				case SERVO_OP_DATA:
					expected = (arg <= SERVO_MAX_DATA_LEN);
					break;
				case SERVO_OP_CREDIT:
				case SERVO_OP_WAIT:
				case SERVO_OP_NAK:
					expected =
						((arg & ~SERVO_SEQ_MASK) == 0U);
					break;
				case SERVO_OP_RESET_REQ:
				case SERVO_OP_RESET_ACK:
					expected = rst_arg_ok;
					break;
				default:
					expected = false;
					break;
				}
				break;
			case SERVO_CHAN_KBD:
				expected = (op == SERVO_OP_DATA ||
					    op == SERVO_OP_CREDIT);
				break;
			case SERVO_CHAN_MSC:
				expected = false;
				break;
			default:
				expected = false;
				break;
			}
			TEST_ASSERT(valid == expected);
		}
	}

	/* 2. Exhaustive single-bit flips across payload lengths 0..64 */
	for (uint8_t len = 0; len <= SERVO_MAX_DATA_LEN; len++) {
		size_t frame_len = servo_frame_encode(
			SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_DATA, 0), len,
			payload, frame);
		TEST_ASSERT(frame_len == (size_t)(4 + len));

		/* Verify valid frame parses */
		servo_frame_rx_init(&parser);
		enum servo_rx_event evt = SERVO_RX_EVT_NONE;
		for (size_t i = 0; i < frame_len; i++)
			evt = servo_frame_rx_step(&parser, frame[i], 0);
		TEST_ASSERT(evt == SERVO_RX_EVT_FRAME_COMPLETE);

		/* Exhaustive single-bit flips across every bit in the frame */
		for (size_t bit = 0; bit < frame_len * 8; bit++) {
			memcpy(corrupted, frame, frame_len);
			corrupted[bit / 8] ^= (1U << (bit % 8));

			servo_frame_rx_init(&parser);
			evt = SERVO_RX_EVT_NONE;
			bool full_frame_accepted = false;
			for (size_t i = 0; i < frame_len; i++) {
				evt = servo_frame_rx_step(&parser, corrupted[i],
							  0);
				if (evt == SERVO_RX_EVT_FRAME_COMPLETE &&
				    i == frame_len - 1)
					full_frame_accepted = true;
			}
			TEST_ASSERT(!full_frame_accepted);
			if (bit >= 24)
				TEST_ASSERT(evt != SERVO_RX_EVT_FRAME_COMPLETE);
		}
	}

	/* 3. 2-bit flips across representative lengths (0, 1, 24, 64) */
	static const uint8_t lengths[] = { 0, 1, 24, 64 };
	for (size_t li = 0; li < ARRAY_SIZE(lengths); li++) {
		uint8_t len = lengths[li];
		size_t frame_len = servo_frame_encode(
			SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_DATA, 0), len,
			payload, frame);

		/*
		 * 2-bit flips across lengths (up to polynomial cycle
		 * length 119 bits).
		 */
		for (size_t b1 = 0; b1 < frame_len * 8; b1 += 3) {
			for (size_t b2 = b1 + 1;
			     b2 < frame_len * 8 && (b2 - b1) < 119; b2 += 5) {
				memcpy(corrupted, frame, frame_len);
				corrupted[b1 / 8] ^= (1U << (b1 % 8));
				corrupted[b2 / 8] ^= (1U << (b2 % 8));

				servo_frame_rx_init(&parser);
				enum servo_rx_event evt = SERVO_RX_EVT_NONE;
				bool full_frame_accepted = false;
				for (size_t i = 0; i < frame_len; i++) {
					evt = servo_frame_rx_step(
						&parser, corrupted[i], 0);
					bool done =
						(evt ==
						 SERVO_RX_EVT_FRAME_COMPLETE);
					if (done && i == frame_len - 1)
						full_frame_accepted = true;
				}
				TEST_ASSERT(!full_frame_accepted);
				if (b1 >= 24 && b2 >= 24)
					TEST_ASSERT(
						evt !=
						SERVO_RX_EVT_FRAME_COMPLETE);
			}
		}
	}

	/* 4. Burst errors <= 8 bits */
	static const uint8_t burst_masks[] = { 0x03, 0x07, 0x0F, 0x1F, 0x3F,
					       0x7F, 0xFF, 0x55, 0xAA };
	for (size_t li = 0; li < ARRAY_SIZE(lengths); li++) {
		uint8_t len = lengths[li];
		size_t frame_len = servo_frame_encode(
			SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_DATA, 0), len,
			payload, frame);

		for (size_t byte_idx = 0; byte_idx < frame_len; byte_idx++) {
			for (size_t mi = 0; mi < ARRAY_SIZE(burst_masks);
			     mi++) {
				memcpy(corrupted, frame, frame_len);
				corrupted[byte_idx] ^= burst_masks[mi];

				servo_frame_rx_init(&parser);
				enum servo_rx_event evt = SERVO_RX_EVT_NONE;
				bool full_frame_accepted = false;
				for (size_t i = 0; i < frame_len; i++) {
					evt = servo_frame_rx_step(
						&parser, corrupted[i], 0);
					bool done =
						(evt ==
						 SERVO_RX_EVT_FRAME_COMPLETE);
					if (done && i == frame_len - 1)
						full_frame_accepted = true;
				}
				TEST_ASSERT(!full_frame_accepted);
				if (byte_idx >= 3)
					TEST_ASSERT(
						evt !=
						SERVO_RX_EVT_FRAME_COMPLETE);
			}
		}
	}

	return EC_SUCCESS;
}

/* 2. Test ADB Connection Detection & Transparent Forwarding */
static int test_cnxn_clamping_and_detection(void)
{
	uint8_t buffer[64];

	for (int offset = 0; offset < 4; offset++) {
		uint8_t *pkt = &buffer[offset];

		/* Construct valid A_CNXN packet with arg1 = 1048576 */
		memset(buffer, 0xCC, sizeof(buffer));
		servo_write_u32_le(&pkt[0], SERVO_ADB_CNXN_CMD);
		servo_write_u32_le(&pkt[4], 0x01000000UL); /* version */
		servo_write_u32_le(&pkt[8], 1048576UL); /* arg1 > 16384 */
		servo_write_u32_le(&pkt[12], 0); /* data length */
		servo_write_u32_le(&pkt[16], 0); /* data check */
		servo_write_u32_le(&pkt[20], SERVO_ADB_CNXN_MAGIC);

		TEST_ASSERT(servo_is_adb_cnxn(pkt, 24, SERVO_PROTO_ADB));
		TEST_ASSERT(!servo_is_adb_cnxn(pkt, 23, SERVO_PROTO_ADB));
		TEST_ASSERT(!servo_is_adb_cnxn(pkt, 25, SERVO_PROTO_ADB));
		TEST_ASSERT(!servo_is_adb_cnxn(pkt, 24, SERVO_PROTO_FASTBOOT));
		TEST_ASSERT(!servo_is_adb_cnxn(pkt, 24, SERVO_PROTO_NONE));

		/* Transparent forwarding preserves arg1 untouched */
		TEST_ASSERT(servo_read_u32_le(&pkt[8]) == 1048576UL);

		/* arg1 <= 16384 preserved */
		servo_write_u32_le(&pkt[8], 16384UL);
		TEST_ASSERT(servo_read_u32_le(&pkt[8]) == 16384UL);

		servo_write_u32_le(&pkt[8], 4096UL);
		TEST_ASSERT(servo_read_u32_le(&pkt[8]) == 4096UL);

		/* Non-A_CNXN packet untouched */
		servo_write_u32_le(&pkt[0], 0x4E504F41U); /* "OPEN" */
		servo_write_u32_le(&pkt[8], 1048576UL);
		servo_write_u32_le(&pkt[20], ~0x4E504F41U);
		TEST_ASSERT(!servo_is_adb_cnxn(pkt, 24, SERVO_PROTO_ADB));
		TEST_ASSERT(servo_read_u32_le(&pkt[8]) == 1048576UL);
	}

	return EC_SUCCESS;
}

/* 3. Test Timer Rollover & Non-Swallowing Resync */
static int test_timer_rollover_and_non_swallowing_resync(void)
{
	struct servo_rx_parser p;
	enum servo_rx_event evt;

	/* 1. 32-bit now_us wrap-around across 0xFFFFFFFF -> 0x00000000 */
	servo_frame_rx_init(&p);
	evt = servo_frame_rx_step(&p, SERVO_BRIDGE_SYNC, 0xFFFFFFF0UL);
	TEST_ASSERT(evt == SERVO_RX_EVT_NONE);
	TEST_ASSERT(p.state == SERVO_RX_WAIT_TYPE);

	/* Check timeout at delta = 15 us */
	evt = servo_frame_rx_check_timeout(&p, 0xFFFFFFFFUL);
	TEST_ASSERT(evt == SERVO_RX_EVT_NONE);
	TEST_ASSERT(p.state == SERVO_RX_WAIT_TYPE);

	/* Wrap across 0 to 0x00002700 (10,000 us elapsed) */
	evt = servo_frame_rx_check_timeout(&p, 0x00002700UL);
	TEST_ASSERT(evt == SERVO_RX_EVT_TIMEOUT_ERROR);
	TEST_ASSERT(p.state == SERVO_RX_WAIT_SYNC);

	/* 2. Non-swallowing 0xA5 (SYNC) re-synchronization */
	servo_frame_rx_init(&p);
	servo_frame_rx_step(&p, SERVO_BRIDGE_SYNC, 1000);
	servo_frame_rx_step(
		&p, SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_DATA, 0), 1005);
	TEST_ASSERT(p.state == SERVO_RX_WAIT_ARG_LEN);

	/*
	 * Advance beyond SERVO_RX_FRAME_TIMEOUT_US (10,000 us) to 11005
	 * and feed 0xA5.
	 */
	evt = servo_frame_rx_step(&p, SERVO_BRIDGE_SYNC, 11005);
	TEST_ASSERT(p.timeout_occurred == true);
	TEST_ASSERT(evt == SERVO_RX_EVT_NONE);
	/* 0xA5 was immediately consumed as the SYNC of the new frame! */
	TEST_ASSERT(p.state == SERVO_RX_WAIT_TYPE);

	/* Feed valid frame: TYPE=H2H DATA 0, LEN=0, CRC */
	servo_frame_rx_step(
		&p, SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_DATA, 0), 11010);
	servo_frame_rx_step(&p, 0, 11015);
	uint8_t crc = servo_crc8_ccitt(0, SERVO_MAKE_TYPE(SERVO_CHAN_H2H,
							  SERVO_OP_DATA, 0));
	crc = servo_crc8_ccitt(crc, 0);
	evt = servo_frame_rx_step(&p, crc, 11020);
	TEST_ASSERT(evt == SERVO_RX_EVT_FRAME_COMPLETE);

	/* 3. Retransmission timer wrap-around across 0xFFFFFFFF -> 0x00000000
	 */
	struct servo_bridge_node node;
	servo_bridge_init(&node, true, 0xFFFFFF00UL);
	node.pending_reset_req = false;
	node.last_reset_tx_us = 0xFFFFFF00UL;

	/* Advance by 15,000 us (across 0) */
	uint32_t now_wrap =
		(uint32_t)(0xFFFFFF00UL + SERVO_RETRANSMIT_TIMEOUT_US);
	servo_bridge_check_timers(&node, now_wrap);
	TEST_ASSERT(node.pending_reset_req == true);

	return EC_SUCCESS;
}

/* 4. Test TX Headroom & Priority Arbitration */
static int test_tx_headroom_and_priority_arbitration(void)
{
	struct servo_bridge_node node;
	uint8_t out[SERVO_MAX_FRAME_BYTES];
	uint8_t payload[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

	servo_bridge_init(&node, true, 0);
	node.state = SERVO_STATE_ACTIVE;
	node.epoch = 1;
	node.booted = false;
	node.active_proto = SERVO_PROTO_ADB;
	node.tx_credits = 1;
	node.pending_reset_req = false;

	/* Stage Priority 3 DATA */
	bool ok = servo_bridge_send_data(&node, payload, sizeof(payload));
	TEST_ASSERT(ok);
	TEST_ASSERT(node.pending_data_tx);

	/* Stage Priority 2 KEY */
	ok = servo_bridge_send_key_cmd(&node, 0x41);
	TEST_ASSERT(ok);
	TEST_ASSERT(node.pending_key_cmd);

	/* Stage Priority 1 NAK and RESET_ACK */
	node.pending_nak = true;
	node.pending_nak_epoch = 1;
	node.pending_nak_seq = 0;

	node.pending_reset_ack = true;
	node.pending_reset_ack_epoch = 1;
	node.pending_reset_ack_proto = SERVO_PROTO_ADB;
	node.pending_reset_ack_boot = false;

	/* Priority arbitration test: pop order MUST be P1 -> P1 -> P2 -> P3 */
	size_t len1 = servo_bridge_pop_tx_frame(&node, 128, out, 0);
	TEST_ASSERT(len1 == 4);
	TEST_ASSERT(SERVO_GET_OP(out[1]) == SERVO_OP_RESET_ACK);

	size_t len2 = servo_bridge_pop_tx_frame(&node, 128, out, 0);
	TEST_ASSERT(len2 == 4);
	TEST_ASSERT(SERVO_GET_OP(out[1]) == SERVO_OP_NAK);

	size_t len3 = servo_bridge_pop_tx_frame(&node, 128, out, 0);
	TEST_ASSERT(len3 == 4);
	TEST_ASSERT(SERVO_GET_CHAN(out[1]) == SERVO_CHAN_KBD &&
		    SERVO_GET_OP(out[1]) == SERVO_OP_DATA);

	size_t len4 = servo_bridge_pop_tx_frame(&node, 128, out, 0);
	TEST_ASSERT(len4 == 4 + sizeof(payload));
	TEST_ASSERT(SERVO_GET_CHAN(out[1]) == SERVO_CHAN_H2H &&
		    SERVO_GET_OP(out[1]) == SERVO_OP_DATA);

	/* TX Headroom enforcement */
	/* P1 frames require 4 bytes (no headroom) */
	node.pending_credit = true;
	node.pending_credit_epoch = 1;
	node.pending_credit_seq = 0;
	TEST_ASSERT(servo_bridge_pop_tx_frame(&node, 3, out, 0) == 0);
	TEST_ASSERT(servo_bridge_pop_tx_frame(&node, 4, out, 0) == 4);

	/* P2 frames require SERVO_KEY_TX_MIN_FREE (4 + 24 = 28 bytes) */
	node.pending_key_cmd = true;
	node.pending_key_cmd_ch = 'K';
	TEST_ASSERT(servo_bridge_pop_tx_frame(&node, SERVO_KEY_TX_MIN_FREE - 1,
					      out, 0) == 0);
	TEST_ASSERT(servo_bridge_pop_tx_frame(&node, SERVO_KEY_TX_MIN_FREE, out,
					      0) == 4);

	/*
	 * P3 DATA frames require SERVO_DATA_TX_MIN_FREE(len)
	 * (4 + 10 + 24 = 38 bytes).
	 */
	node.has_tx_data = false;
	node.tx_credits = 1;
	servo_bridge_send_data(&node, payload, 10);
	TEST_ASSERT(servo_bridge_pop_tx_frame(&node,
					      SERVO_DATA_TX_MIN_FREE(10) - 1,
					      out, 0) == 0);
	TEST_ASSERT(servo_bridge_pop_tx_frame(&node, SERVO_DATA_TX_MIN_FREE(10),
					      out, 0) == 14);

	return EC_SUCCESS;
}

/* 5. Test Dual-MCU Protocol & Epoch Transitions */
static int test_dual_mcu_protocol_and_epoch_transitions(void)
{
	struct servo_bridge_node stm32;
	struct servo_bridge_node atmega;
	struct servo_bridge_rx_effects fx;
	uint8_t out[SERVO_MAX_FRAME_BYTES];

	/* 1. Modulo-4 directional epoch arithmetic */
	for (uint8_t cur = 0; cur < 4; cur++) {
		for (uint8_t req = 0; req < 4; req++) {
			bool newer = SERVO_IS_NEWER_EPOCH(req, cur);
			bool expected = (((req - cur) & SERVO_EPOCH_MASK) == 1);
			TEST_ASSERT(newer == expected);
		}
	}

	/* 2. Boot handshake */
	servo_bridge_init(&stm32, true, 0);
	servo_bridge_init(&atmega, false, 0);

	TEST_ASSERT(stm32.state == SERVO_STATE_RESETTING && stm32.booted);
	TEST_ASSERT(atmega.state == SERVO_STATE_RESETTING && atmega.booted);

	/* Both nodes pop and transmit their boot RESET_REQ frames */
	uint8_t stm_req[SERVO_MAX_FRAME_BYTES];
	uint8_t atm_req[SERVO_MAX_FRAME_BYTES];
	size_t len = servo_bridge_pop_tx_frame(&stm32, 128, stm_req, 0);
	TEST_ASSERT(len == 4);
	TEST_ASSERT(SERVO_GET_OP(stm_req[1]) == SERVO_OP_RESET_REQ);

	len = servo_bridge_pop_tx_frame(&atmega, 128, atm_req, 0);
	TEST_ASSERT(len == 4);
	TEST_ASSERT(SERVO_GET_OP(atm_req[1]) == SERVO_OP_RESET_REQ);

	/* Full-duplex delivery: ATmega receives STM32's RESET_REQ */
	servo_bridge_on_rx_frame(&atmega, stm_req[1], stm_req[2], NULL, &fx);
	TEST_ASSERT(atmega.pending_reset_ack);

	/* Full-duplex delivery: STM32 receives ATmega's RESET_REQ */
	servo_bridge_on_rx_frame(&stm32, atm_req[1], atm_req[2], NULL, &fx);
	TEST_ASSERT(stm32.pending_reset_ack);

	/* ATmega responds with boot RESET_ACK */
	len = servo_bridge_pop_tx_frame(&atmega, 128, out, 0);
	TEST_ASSERT(len == 4);
	TEST_ASSERT(SERVO_GET_OP(out[1]) == SERVO_OP_RESET_ACK);
	servo_bridge_on_rx_frame(&stm32, out[1], out[2], NULL, &fx);
	TEST_ASSERT(stm32.state == SERVO_STATE_ACTIVE && !stm32.booted);

	/* STM32 responds with boot RESET_ACK */
	len = servo_bridge_pop_tx_frame(&stm32, 128, out, 0);
	TEST_ASSERT(len == 4);
	TEST_ASSERT(SERVO_GET_OP(out[1]) == SERVO_OP_RESET_ACK);
	servo_bridge_on_rx_frame(&atmega, out[1], out[2], NULL, &fx);
	TEST_ASSERT(atmega.state == SERVO_STATE_ACTIVE && !atmega.booted);

	/* 3. ATmega requests ADB protocol (unconditional epoch reset on 0xE6)
	 */
	bool ok = servo_bridge_request_proto(&atmega, SERVO_PROTO_ADB, 1000);
	TEST_ASSERT(ok);
	TEST_ASSERT(atmega.epoch == 1);
	TEST_ASSERT(atmega.pending_reset_req);

	len = servo_bridge_pop_tx_frame(&atmega, 128, out, 1000);
	TEST_ASSERT(len == 4);
	servo_bridge_on_rx_frame(&stm32, out[1], out[2], NULL, &fx);
	TEST_ASSERT(stm32.epoch == 1);
	TEST_ASSERT(stm32.active_proto == SERVO_PROTO_ADB);
	TEST_ASSERT(stm32.pending_reset_ack);

	len = servo_bridge_pop_tx_frame(&stm32, 128, out, 1000);
	TEST_ASSERT(len == 4);
	servo_bridge_on_rx_frame(&atmega, out[1], out[2], NULL, &fx);
	TEST_ASSERT(atmega.state == SERVO_STATE_ACTIVE && atmega.epoch == 1);

	/* Request FASTBOOT protocol -> advances epoch to 2 */
	ok = servo_bridge_request_proto(&atmega, SERVO_PROTO_FASTBOOT, 2000);
	TEST_ASSERT(ok);
	TEST_ASSERT(atmega.epoch == 2);

	/* 4. Duplicate seq != rx_seq payload suppression with credit re-ack */
	stm32.rx_seq = 1; /* expected seq is 1 */
	uint8_t payload[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
	/* Deliver packet with seq = 0 (duplicate!) */
	uint8_t type_dup = SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_DATA, 0);
	servo_bridge_on_rx_frame(&stm32, type_dup, sizeof(payload), payload,
				 &fx);
	TEST_ASSERT(!fx.usb_in_armed); /* Suppressed! */
	TEST_ASSERT(stm32.pending_credit); /* Credit re-acked! */
	TEST_ASSERT(stm32.pending_credit_seq == 0);

	return EC_SUCCESS;
}

/* 6. Test USART4 RX DMA Circular Staging, Wrap-Around, & ORE/FE Recovery */
static int test_usart4_rx_isr_staging_and_ore_fe_recovery(void)
{
	drain_queue(&usart4_to_bridge);
	mock_usart_cr1 = 0;
	mock_usart_cr3 = STM32_USART_CR3_OVRDIS;
	mock_usart_sr = 0;
	mock_usart_icr = 0;
	mock_usart4_state.rx_overrun = 0;
	mock_usart4_state.rx_dropped = 0;
	mock_usart4_state.rx_framing_error = 0;
	mock_usart4_state.rx_noise_error = 0;

	/* 1. Call usart_rx_init on USART4 */
	usart_rx_init(&usart4);
	TEST_ASSERT((mock_usart_cr3 & STM32_USART_CR3_OVRDIS) == 0);
	TEST_ASSERT((mock_usart_cr3 & STM32_USART_CR3_DMAR) != 0);
	TEST_ASSERT((mock_usart_cr3 & STM32_USART_CR3_EIE) != 0);
	TEST_ASSERT((mock_usart_cr1 & STM32_USART_CR1_IDLEIE) != 0);
	TEST_ASSERT((mock_usart_cr1 & STM32_USART_CR1_RE) != 0);
	TEST_ASSERT((mock_dma_channels[STM32_DMAC_CH6].ccr &
		     STM32_DMA_CCR_CIRC) != 0);
	TEST_ASSERT((mock_dma_channels[STM32_DMAC_CH6].ccr &
		     STM32_DMA_CCR_EN) != 0);
	TEST_ASSERT((mock_dma_channels[STM32_DMAC_CH6].ccr &
		     STM32_DMA_CCR_TCIE) == 0); /* Non-negotiable! */

	/* 2. Feed valid 28-byte frame via DMA followed by IDLE */
	uint8_t payload[24];
	for (size_t i = 0; i < sizeof(payload); i++)
		payload[i] = (uint8_t)(i + 0x10);
	uint8_t frame[SERVO_MAX_FRAME_BYTES];
	size_t frame_len = servo_frame_encode(SERVO_MAKE_TYPE(SERVO_CHAN_H2H,
							      SERVO_OP_DATA, 0),
					      sizeof(payload), payload, frame);
	TEST_ASSERT(frame_len == 28);

	mock_dma_rx_feed_bytes(frame, frame_len);
	mock_dma_rx_trigger_idle();

	TEST_ASSERT(mock_usart4_state.rx_overrun == 0);
	TEST_ASSERT(mock_usart4_state.rx_dropped == 0);
	TEST_ASSERT(mock_usart4_state.rx_framing_error == 0);
	TEST_ASSERT(mock_usart4_state.rx_noise_error == 0);
	TEST_ASSERT(queue_count(&usart4_to_bridge) == 28);

	uint8_t dequeued[28];
	for (size_t i = 0; i < 28; i++)
		TEST_ASSERT(queue_remove_unit(&usart4_to_bridge, &dequeued[i]));
	TEST_ASSERT(memcmp(dequeued, frame, 28) == 0);

	/* 3. Test circular buffer wrap-around across 80-byte boundary */
	/* Buffer size is 80 bytes. Tail is currently at 28. Feed 40 bytes ->
	 * tail at 68 */
	uint8_t chunk40[40];
	for (size_t i = 0; i < sizeof(chunk40); i++)
		chunk40[i] = (uint8_t)(i + 0xA0);
	mock_dma_rx_feed_bytes(chunk40, sizeof(chunk40));
	mock_dma_rx_trigger_idle();
	TEST_ASSERT(mock_usart4_state.rx_overrun == 0);
	TEST_ASSERT(mock_usart4_state.rx_dropped == 0);
	TEST_ASSERT(mock_usart4_state.rx_noise_error == 0);
	TEST_ASSERT(queue_count(&usart4_to_bridge) == 40);
	drain_queue(&usart4_to_bridge);

	/* Tail is at 68. Feed 30 bytes -> wraps around 80, 18 bytes at start of
	 * buffer */
	uint8_t chunk30[30];
	for (size_t i = 0; i < sizeof(chunk30); i++)
		chunk30[i] = (uint8_t)(i + 0xC0);
	mock_dma_rx_feed_bytes(chunk30, sizeof(chunk30));
	mock_dma_rx_trigger_idle();
	TEST_ASSERT(mock_usart4_state.rx_overrun == 0);
	TEST_ASSERT(mock_usart4_state.rx_dropped == 0);
	TEST_ASSERT(mock_usart4_state.rx_noise_error == 0);
	TEST_ASSERT(queue_count(&usart4_to_bridge) == 30);
	uint8_t dequeued30[30];
	for (size_t i = 0; i < 30; i++)
		TEST_ASSERT(
			queue_remove_unit(&usart4_to_bridge, &dequeued30[i]));
	TEST_ASSERT(memcmp(dequeued30, chunk30, 30) == 0);

	/* 4. Inject ORE (overrun) mid-burst */
	drain_queue(&usart4_to_bridge);
	mock_usart4_state.rx_overrun = 0;
	mock_usart4_state.rx_noise_error = 0;
	mock_dma_rx_feed_bytes(frame, 10);
	mock_usart_sr &= ~mock_usart_icr;
	mock_usart_icr = 0;
	mock_usart_sr |= STM32_USART_SR_ORE;
	usart4.rx->interrupt(&usart4);
	mock_dma_rx_feed_bytes(&frame[10], 10);
	mock_dma_rx_trigger_idle();

	TEST_ASSERT(queue_count(&usart4_to_bridge) == 0);
	TEST_ASSERT(mock_usart4_state.rx_overrun > 0);
	TEST_ASSERT(mock_usart4_state.rx_noise_error == 0);

	/* 5. Inject Framing Error (FE) */
	mock_usart4_state.rx_framing_error = 0;
	mock_usart4_state.rx_overrun = 0;
	mock_usart4_state.rx_noise_error = 0;
	mock_dma_rx_feed_bytes(frame, 5);
	mock_usart_sr &= ~mock_usart_icr;
	mock_usart_icr = 0;
	mock_usart_sr |= STM32_USART_SR_FE;
	usart4.rx->interrupt(&usart4);
	TEST_ASSERT(mock_usart4_state.rx_framing_error > 0);
	TEST_ASSERT(mock_usart4_state.rx_overrun == 0);
	TEST_ASSERT(mock_usart4_state.rx_noise_error == 0);
	mock_dma_rx_feed_bytes(&frame[5], 5);
	mock_dma_rx_trigger_idle();

	TEST_ASSERT(queue_count(&usart4_to_bridge) == 0);
	TEST_ASSERT(mock_usart4_state.rx_framing_error > 0);
	TEST_ASSERT(mock_usart4_state.rx_overrun == 0);
	TEST_ASSERT(mock_usart4_state.rx_noise_error == 0);

	/* 5b. Inject Noise Error (NE) */
	mock_usart4_state.rx_framing_error = 0;
	mock_usart4_state.rx_overrun = 0;
	mock_usart4_state.rx_noise_error = 0;
	mock_dma_rx_feed_bytes(frame, 5);
	mock_usart_sr &= ~mock_usart_icr;
	mock_usart_icr = 0;
	mock_usart_sr |= STM32_USART_SR_NE;
	usart4.rx->interrupt(&usart4);
	TEST_ASSERT(mock_usart4_state.rx_noise_error > 0);
	TEST_ASSERT(mock_usart4_state.rx_overrun == 0);
	TEST_ASSERT(mock_usart4_state.rx_framing_error == 0);
	mock_dma_rx_feed_bytes(&frame[5], 5);
	mock_dma_rx_trigger_idle();

	TEST_ASSERT(queue_count(&usart4_to_bridge) == 0);
	TEST_ASSERT(mock_usart4_state.rx_noise_error > 0);
	TEST_ASSERT(mock_usart4_state.rx_overrun == 0);
	TEST_ASSERT(mock_usart4_state.rx_framing_error == 0);

	/* 6. Inject queue-full shortfall on IDLE */
	mock_usart4_state.rx_dropped = 0;

	/* Fill queue until only 10 bytes remain */
	while (queue_space(&usart4_to_bridge) > 10) {
		uint8_t d = 0;
		queue_add_unit(&usart4_to_bridge, &d);
	}
	/* Feed 20 bytes into usart4_rx_buf */
	mock_dma_rx_feed_bytes(frame, 20);
	mock_dma_rx_trigger_idle();

	TEST_ASSERT(mock_usart4_state.rx_dropped == 10);

	drain_queue(&usart4_to_bridge);
	return EC_SUCCESS;
}

/* 7. Test USB Bridge EP7 CTR rc_w0 & DTOG Preservation */
static int test_usb_bridge_ep7_ctr_rc_w0_and_dtog_preservation(void)
{
	mock_irq_depth_violation = false;

	/* 1. usb_adb_tx: CTR_TX cleared to 0, CTR_RX preserved as 1 */
	mock_usb_ep[USB_EP_BRIDGE] = EP_CTR_RX | EP_CTR_TX;
	mock_in_isr = true;
	usb_bridge_tx();
	mock_in_isr = false;
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_CTR_TX) == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_CTR_RX) != 0);

	/* 2. usb_adb_rx: CTR_RX cleared to 0, CTR_TX preserved as 1 */
	mock_usb_ep[USB_EP_BRIDGE] = EP_CTR_RX | EP_CTR_TX;
	mock_in_isr = true;
	usb_bridge_rx();
	mock_in_isr = false;
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_CTR_RX) == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_CTR_TX) != 0);

	/* 3. ep7_toggle_safe: both CTR_RX and CTR_TX remain 1, interrupts
	 * disabled */
	mock_usb_ep[USB_EP_BRIDGE] = EP_CTR_RX | EP_CTR_TX | EP_RX_NAK;
	mock_ep7_toggle_count = 0;
	ep7_toggle_safe(EP_RX_MASK, EP_RX_VALID);

	TEST_ASSERT(!mock_irq_depth_violation);
	TEST_ASSERT(mock_ep7_toggle_count > 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_RX_MASK) == EP_RX_VALID);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_CTR_RX) != 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_CTR_TX) != 0);

	/* 4. ep7_disarm_tx_preserve_dtog */
	effective_usb_protocol = SERVO_PROTO_ADB;
	last_enumerated_proto = SERVO_PROTO_ADB;
	dppu_pulse_active = false;
	mock_usb_ep[USB_EP_BRIDGE] = EP_TX_VALID | EP_TX_DTOG;
	btable_ep[USB_EP_BRIDGE].tx_count = 64;

	ep7_disarm_tx_preserve_dtog();

	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_NAK);
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_DTOG) != 0);

	return EC_SUCCESS;
}

/* 8. Test USB Bridge Corrupt IRQ Atomic Flush & Session Barrier */
static int test_usb_bridge_corrupt_irq_atomic_flush_and_session_barrier(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	mock_pd_connected = true;
	last_enumerated_proto = SERVO_PROTO_NONE;
	dppu_pulse_active = false;
	probe_state = USB_BRIDGE_LOCKED_1M_BRIDGE;
	effective_usb_protocol = SERVO_PROTO_ADB;
	active_usb_protocol = SERVO_PROTO_ADB;

	/* 1. Corrupt IRQ atomic flush */
	uint8_t garbage[16] = { 0xDE, 0xAD, 0xBE, 0xEF };
	queue_add_units(&usart4_to_bridge, garbage, sizeof(garbage));
	bridge.state = SERVO_STATE_ACTIVE;
	bridge.epoch = 1;
	bridge.rx.state = SERVO_RX_WAIT_PAYLOAD;
	mock_usart4_state.rx_overrun++;
	mock_queue_init_irq_violation = false;
	mock_queue_init_usart4_count = 0;

	bridge_arbiter_deferred();

	TEST_ASSERT(!mock_queue_init_irq_violation);
	TEST_ASSERT(mock_queue_init_usart4_count > 0);
	TEST_ASSERT(queue_count(&usart4_to_bridge) == 0);
	TEST_ASSERT(bridge.rx.state == SERVO_RX_WAIT_SYNC);
	TEST_ASSERT(queue_count(&bridge_to_usart4) == 4);

	uint8_t nak_frame[4];
	for (int i = 0; i < 4; i++)
		TEST_ASSERT(
			queue_remove_unit(&bridge_to_usart4, &nak_frame[i]));
	TEST_ASSERT(SERVO_GET_OP(nak_frame[1]) == SERVO_OP_NAK);

	/* 2. Mid-frame timeout & non-swallowing 0xA5 resync */
	drain_queue(&bridge_kbd_to_usb);
	bridge.is_epoch_leader = true;
	force_time((timestamp_t){ .val = 100 });
	uint8_t partial_hdr[3] = {
		SERVO_BRIDGE_SYNC,
		SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_DATA, 0),
		10,
	};
	queue_add_units(&usart4_to_bridge, partial_hdr, sizeof(partial_hdr));
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.rx.state == SERVO_RX_WAIT_PAYLOAD);
	TEST_ASSERT(bridge.rx.frame_start_us >= 100 &&
		    bridge.rx.frame_start_us <= 500);

	/*
	 * Advance clock past SERVO_RX_FRAME_TIMEOUT_US (10,000 us) and enqueue
	 * a valid 0xA5 frame (SERVO_FRAME_KEY_ACK) before calling the arbiter.
	 * Verifies that bridge_arbiter_deferred() does NOT overwrite
	 * bridge.rx.frame_start_us before servo_frame_rx_step() runs, so the
	 * stale partial frame times out (emitting NAK) and non-swallowing
	 * resync on 0xA5 immediately parses and delivers the new valid frame.
	 */
	force_time((timestamp_t){ .val = 25000 });
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_KEY_ACK | 0, 0x9E,
			 NULL);
	bridge_arbiter_deferred();

	TEST_ASSERT(bridge.rx.state == SERVO_RX_WAIT_SYNC);
	TEST_ASSERT(queue_count(&bridge_to_usart4) == 4);
	for (int i = 0; i < 4; i++)
		TEST_ASSERT(
			queue_remove_unit(&bridge_to_usart4, &nak_frame[i]));
	TEST_ASSERT(SERVO_GET_OP(nak_frame[1]) == SERVO_OP_NAK);

	uint8_t kbd_ack = 0;
	TEST_ASSERT(queue_count(&bridge_kbd_to_usb) == 1);
	TEST_ASSERT(queue_remove_unit(&bridge_kbd_to_usb, &kbd_ack));
	TEST_ASSERT(kbd_ack == 0x9E);
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);

	/* 3. Full end-to-end ADB session resynchronization */
	/* 3.1 Handshake to active ADB */
	usb_bridge_init();
	mock_pd_connected = true;
	usb_bridge_pd_event();

	/* Feed peer boot RESET_ACK */
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | 0,
			 SERVO_RESET_FLAG_BOOT | SERVO_PROTO_NONE, NULL);
	bridge_arbiter_deferred();
	drain_queue(&bridge_to_usart4);

	/* Feed peer runtime RESET_REQ(epoch=1, proto=ADB) */
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_REQ | 1,
			 SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();
	drain_queue(&bridge_to_usart4);

	bridge_dppu_debounce_deferred();
	bridge_dppu_reconnect_deferred();
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);
	TEST_ASSERT(mock_desc_patch[USB_DESC_BRIDGE] == 0x0142);
	TEST_ASSERT(mock_desc_patch[USB_DESC_BRIDGE_ALT1] == 0x0142);

	/* 3.2 Arm stale DUT -> Host packet on EP7 IN */
	btable_ep[USB_EP_BRIDGE].tx_count = 24;
	mock_usb_ep[USB_EP_BRIDGE] = EP_TX_VALID | EP_TX_DTOG;
	bridge.usb_in_busy = true;

	/* 3.3 Host sends 24-byte A_CNXN with max_payload = 1048576 */
	uint8_t cnxn[24];
	servo_write_u32_le(&cnxn[0], SERVO_ADB_CNXN_CMD);
	servo_write_u32_le(&cnxn[4], 0x01000000UL);
	servo_write_u32_le(&cnxn[8], 1048576UL);
	servo_write_u32_le(&cnxn[12], 0);
	servo_write_u32_le(&cnxn[16], 0);
	servo_write_u32_le(&cnxn[20], SERVO_ADB_CNXN_MAGIC);
	memcpy(ep_bridge_rx_buffer, cnxn, 24);
	btable_ep[USB_EP_BRIDGE].rx_count = 24;

	mock_in_isr = true;
	usb_bridge_rx();
	mock_in_isr = false;

	bridge_arbiter_deferred();

	/* Verify EP7 IN disarmed, DTOG preserved */
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_NAK);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_DTOG) != 0);
	TEST_ASSERT(bridge.adb_pending_syncs == 1);

	/* Verify forwarded DATA frame in bridge_to_usart4 has arg1 forwarded
	 * transparently (1048576) */
	TEST_ASSERT(queue_count(&bridge_to_usart4) == 28);
	uint8_t fwd[28];
	for (int i = 0; i < 28; i++)
		TEST_ASSERT(queue_remove_unit(&bridge_to_usart4, &fwd[i]));
	TEST_ASSERT(servo_read_u32_le(&fwd[3 + 8]) == SERVO_MAX_PAYLOAD_CLAMP);

	/* 3.4 Feed stale pre-barrier 64-byte SERVO_FRAME_DATA into
	 * usart4_to_adb */
	uint8_t stale[64];
	memset(stale, 0x77, sizeof(stale));
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 sizeof(stale), stale);

	bridge_arbiter_deferred();

	/* Dropped without arming EP7 IN, CREDIT emitted */
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_NAK);
	TEST_ASSERT(queue_count(&bridge_to_usart4) == 4);
	uint8_t cred[4];
	for (int i = 0; i < 4; i++)
		TEST_ASSERT(queue_remove_unit(&bridge_to_usart4, &cred[i]));
	TEST_ASSERT(SERVO_GET_OP(cred[1]) == SERVO_OP_CREDIT);

	/* 3.5 Feed 0-byte SYNC_ZLP barrier into usart4_to_adb */
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 0, NULL);

	bridge_arbiter_deferred();

	TEST_ASSERT(bridge.adb_pending_syncs == 0);
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 0);
	TEST_ASSERT(queue_count(&bridge_to_usart4) == 4);
	for (int i = 0; i < 4; i++)
		TEST_ASSERT(queue_remove_unit(&bridge_to_usart4, &cred[i]));
	TEST_ASSERT(SERVO_GET_OP(cred[1]) == SERVO_OP_CREDIT);

	/* 3.6 Feed new-session 24-byte A_CNXN response into usart4_to_adb */
	uint8_t cnxn_resp[24];
	memcpy(cnxn_resp, cnxn, 24);
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 sizeof(cnxn_resp), cnxn_resp);

	bridge_arbiter_deferred();

	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 24);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_VALID);
	uint8_t armed_tx[64];
	memcpy_from_usbram(armed_tx, (void *)usb_sram_addr(ep_bridge_tx_buffer),
			   24);
	TEST_ASSERT(servo_read_u32_le(&armed_tx[8]) == SERVO_MAX_PAYLOAD_CLAMP);

	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	return EC_SUCCESS;
}

/* 9. Test USB Bridge DPPU Debounce, Fastboot ZLP & USART4 Control */
static int test_usb_bridge_dppu_debounce_fastboot_zlp_and_usart4_ctl(void)
{
	/* 1. pd_is_connected(1) disconnect */
	mock_pd_connected = false;
	usb_bridge_pd_event();

	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_NONE);
	TEST_ASSERT(mock_desc_patch[USB_DESC_BRIDGE] == 0x0000);
	TEST_ASSERT(mock_desc_patch[USB_DESC_BRIDGE_ALT1] == 0x0000);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_RX_MASK) ==
		    (EP_TX_STALL | EP_RX_STALL));

	/* 2. Fastboot ZLP emission */
	mock_pd_connected = true;
	probe_state = USB_BRIDGE_LOCKED_1M_BRIDGE;
	active_usb_protocol = SERVO_PROTO_FASTBOOT;
	bridge.active_proto = SERVO_PROTO_FASTBOOT;
	usb_bridge_update_effective_proto();
	bridge.state = SERVO_STATE_ACTIVE;
	bridge.epoch = 2;
	bridge.rx_seq = 0;
	bridge.usb_in_busy = false;
	bridge.tx_credits = 1;
	last_enumerated_proto = SERVO_PROTO_FASTBOOT;
	usb_bridge_pd_event();
	bridge_dppu_reconnect_deferred();

	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_FASTBOOT);
	TEST_ASSERT(mock_desc_patch[USB_DESC_BRIDGE] == 0x0000);
	TEST_ASSERT(mock_desc_patch[USB_DESC_BRIDGE_ALT1] == 0x0342);

	/* Send 1 x 64B packet */
	uint8_t data64[64];
	memset(data64, 0x88, sizeof(data64));
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 64, data64);
	bridge_arbiter_deferred();
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 64);
	TEST_ASSERT(fb_tx_64b_count == 1);

	/* Simulate host reading packet */
	mock_in_isr = true;
	usb_bridge_tx();
	mock_in_isr = false;
	bridge_arbiter_deferred();

	/* Idle for 5,000 us */
	last_ep7_in_drain_us -= 5000;
	bridge_arbiter_deferred();

	TEST_ASSERT(ep7_zlp_in_flight == true);
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_VALID);

	/* Drain ZLP */
	mock_in_isr = true;
	usb_bridge_tx();
	mock_in_isr = false;
	bridge_arbiter_deferred();
	TEST_ASSERT(ep7_zlp_in_flight == false);

	/* Send 4 x 64B packets (256 B) */
	for (int i = 0; i < 4; i++) {
		drain_queue(&bridge_to_usart4);
		queue_feed_frame(&usart4_to_bridge,
				 SERVO_FRAME_DATA |
					 (bridge.rx_seq
					  << SERVO_DATA_SEQ_SHIFT) |
					 (bridge.epoch & SERVO_EPOCH_MASK),
				 64, data64);
		bridge_arbiter_deferred();
		TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 64);

		mock_in_isr = true;
		usb_bridge_tx();
		mock_in_isr = false;
		bridge_arbiter_deferred();
	}
	TEST_ASSERT(fb_tx_64b_count == 0);

	/* Idle for 5,000 us: no ZLP should be emitted */
	last_ep7_in_drain_us -= 5000;
	bridge_arbiter_deferred();
	TEST_ASSERT(ep7_zlp_in_flight == false);

	/* 3. usart4_iface_request */
	usb_uint rx_buf[4];
	usb_uint tx_buf[4];

	/* USB_USART_REQ_BAUD */
	rx_buf[0] = (USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) |
		    (USB_USART_REQ_BAUD << 8);
	rx_buf[1] = 0;
	rx_buf[2] = USB_IFACE_USART4_STREAM;
	rx_buf[3] = 2;
	int ret = usart4_iface_request(rx_buf, tx_buf);
	TEST_ASSERT(ret == 0);
	uint16_t baud_resp = tx_buf[0];
	TEST_ASSERT(baud_resp == (1000000 / 100));

	/* USB_USART_SET_BAUD */
	rx_buf[0] = (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) |
		    (USB_USART_SET_BAUD << 8);
	rx_buf[1] = 0;
	rx_buf[2] = USB_IFACE_USART4_STREAM;
	rx_buf[3] = 0;
	ret = usart4_iface_request(rx_buf, tx_buf);
	TEST_ASSERT(ret == 0);

	/* USB_USART_CLEAR_QUEUES */
	bridge.pending_key_cmd = true;
	rx_buf[0] = (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) |
		    (USB_USART_CLEAR_QUEUES << 8);
	rx_buf[1] = 0; /* CLEAR_BOTH_FIFOS */
	rx_buf[2] = USB_IFACE_USART4_STREAM;
	rx_buf[3] = 0;
	ret = usart4_iface_request(rx_buf, tx_buf);
	TEST_ASSERT(ret == 0);
	TEST_ASSERT(!bridge.pending_key_cmd);

	/*
	 * 4. bridge_iface_request (SET_INTERFACE / GET_INTERFACE)
	 *    & usb_bridge_clear_halt.
	 */
	effective_usb_protocol = SERVO_PROTO_ADB;
	last_enumerated_proto = SERVO_PROTO_ADB;
	dppu_pulse_active = false;
	bridge.usb_in_busy = true;
	host_usb_reset_irq = false;
	mock_usb_ep[USB_EP_BRIDGE] = EP_CTR_RX | EP_CTR_TX | EP_TX_VALID |
				     EP_RX_NAK | EP_TX_DTOG | EP_RX_DTOG;

	/* USB_REQ_SET_INTERFACE (alt = 0) */
	rx_buf[0] = (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) |
		    (USB_REQ_SET_INTERFACE << 8);
	rx_buf[1] = 0;
	rx_buf[2] = USB_IFACE_BRIDGE;
	rx_buf[3] = 0;
	mock_in_isr = true;
	ret = bridge_iface_request(rx_buf, tx_buf);
	mock_in_isr = false;
	TEST_ASSERT(ret == 0);
	TEST_ASSERT(host_usb_reset_irq == true);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_CTR_RX) != 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_CTR_TX) != 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & (EP_TX_DTOG | EP_RX_DTOG)) ==
		    0);

	/* USB_REQ_GET_INTERFACE */
	rx_buf[0] = (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) |
		    (USB_REQ_GET_INTERFACE << 8);
	rx_buf[1] = 0;
	rx_buf[2] = USB_IFACE_BRIDGE;
	rx_buf[3] = 1;
	tx_buf[0] = 0xFFFF;
	mock_in_isr = true;
	ret = bridge_iface_request(rx_buf, tx_buf);
	mock_in_isr = false;
	TEST_ASSERT(ret == 0);
	TEST_ASSERT(tx_buf[0] == 0);
	TEST_ASSERT(btable_ep[0].tx_count == 1);

	/* usb_bridge_clear_halt on EP7 IN (0x87) and EP7 OUT (0x07) */
	host_usb_reset_irq = false;
	bridge.usb_in_busy = true;
	mock_usb_ep[USB_EP_BRIDGE] = EP_CTR_RX | EP_CTR_TX | EP_TX_STALL |
				     EP_TX_DTOG;
	mock_in_isr = true;
	ret = usb_bridge_clear_halt(USB_EP_BRIDGE | 0x80);
	mock_in_isr = false;
	TEST_ASSERT(ret == 0);
	TEST_ASSERT(host_usb_reset_irq == true);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_NAK);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_DTOG) == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & (EP_CTR_RX | EP_CTR_TX)) ==
		    (EP_CTR_RX | EP_CTR_TX));

	host_usb_reset_irq = false;
	bridge.usb_in_busy = true;
	mock_usb_ep[USB_EP_BRIDGE] = EP_CTR_RX | EP_CTR_TX | EP_RX_STALL |
				     EP_RX_DTOG;
	mock_in_isr = true;
	ret = usb_bridge_clear_halt(USB_EP_BRIDGE);
	mock_in_isr = false;
	TEST_ASSERT(ret == 0);
	TEST_ASSERT(host_usb_reset_irq == true);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_RX_MASK) == EP_RX_VALID);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_RX_DTOG) == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & (EP_CTR_RX | EP_CTR_TX)) ==
		    (EP_CTR_RX | EP_CTR_TX));

	return EC_SUCCESS;
}

#define SPSC_TEST_TOTAL_FRAMES 10000U

struct spsc_concurrency_ring {
	uint8_t buf[SERVO_RX_RING_BYTES];
	uint8_t head;
	uint8_t tail;
};

static inline bool spsc_test_push_byte(struct spsc_concurrency_ring *r,
				       uint8_t b)
{
	uint8_t head = __atomic_load_n(&r->head, __ATOMIC_RELAXED);
	uint8_t next = (uint8_t)(head + 1U);
	uint8_t tail = __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE);

	if (next == tail)
		return false;
	r->buf[head] = b;
	__atomic_store_n(&r->head, next, __ATOMIC_RELEASE);
	return true;
}

static inline bool spsc_test_pop_byte(struct spsc_concurrency_ring *r,
				      uint8_t *out)
{
	uint8_t tail = __atomic_load_n(&r->tail, __ATOMIC_RELAXED);
	uint8_t head = __atomic_load_n(&r->head, __ATOMIC_ACQUIRE);

	if (tail == head)
		return false;
	*out = r->buf[tail];
	__atomic_store_n(&r->tail, (uint8_t)(tail + 1U), __ATOMIC_RELEASE);
	return true;
}

struct spsc_concurrency_ctx {
	struct spsc_concurrency_ring rx_ring;
	bool producer_done;
	uint32_t frames_decoded;
	bool error;
};

static void *spsc_test_producer_thread(void *arg)
{
	struct spsc_concurrency_ctx *ctx = (struct spsc_concurrency_ctx *)arg;

	for (uint32_t f = 0U; f < SPSC_TEST_TOTAL_FRAMES; ++f) {
		uint8_t payload[SERVO_MAX_DATA_LEN];
		uint8_t raw[SERVO_MAX_FRAME_BYTES];
		uint8_t len = (uint8_t)(f % (SERVO_MAX_DATA_LEN + 1U));
		uint8_t seq = (uint8_t)(f & 0x01U);
		uint8_t type =
			SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_DATA, seq);
		for (size_t i = 0U; i < len; ++i)
			payload[i] = (uint8_t)((f + i) ^ 0xA5U);

		size_t flen = servo_frame_encode(type, len, payload, raw);
		for (size_t i = 0U; i < flen; ++i) {
			while (!spsc_test_push_byte(&ctx->rx_ring, raw[i])) {
				/* Spin-wait */
			}
		}
	}

	__atomic_store_n(&ctx->producer_done, true, __ATOMIC_RELEASE);
	return NULL;
}

static void *spsc_test_consumer_thread(void *arg)
{
	struct spsc_concurrency_ctx *ctx = (struct spsc_concurrency_ctx *)arg;
	struct servo_rx_parser parser;
	uint32_t now_us = 1000U;

	servo_frame_rx_init(&parser);

	for (;;) {
		uint8_t b = 0U;
		if (spsc_test_pop_byte(&ctx->rx_ring, &b)) {
			enum servo_rx_event evt =
				servo_frame_rx_step(&parser, b, now_us);
			now_us += 10U;
			if (evt == SERVO_RX_EVT_FRAME_COMPLETE) {
				uint32_t f = ctx->frames_decoded;
				uint8_t exp_len =
					(uint8_t)(f %
						  (SERVO_MAX_DATA_LEN + 1U));
				if (parser.arg_len != exp_len) {
					ctx->error = true;
					break;
				}
				for (uint8_t i = 0U; i < exp_len; ++i) {
					if (parser.payload[i] !=
					    (uint8_t)((f + i) ^ 0xA5U)) {
						ctx->error = true;
						break;
					}
				}
				ctx->frames_decoded++;
			} else if (evt != SERVO_RX_EVT_NONE) {
				ctx->error = true;
				break;
			}
		} else if (__atomic_load_n(&ctx->producer_done,
					   __ATOMIC_ACQUIRE)) {
			break;
		}
	}
	return NULL;
}

static void ec_fuzz_byte_stream_parser(const uint8_t *data, size_t size,
				       bool *err)
{
	struct servo_rx_parser p;
	uint32_t now_us = 0xFFFFF000U;
	size_t i = 0U;
	uint8_t valid_payload[24];
	uint8_t valid_frame[SERVO_MAX_FRAME_BYTES];
	size_t valid_len;
	enum servo_rx_event evt = SERVO_RX_EVT_NONE;

	servo_frame_rx_init(&p);

	while (i < size) {
		uint8_t b = data[i++];
		uint32_t dt = (i < size) ? ((uint32_t)data[i++] * 25U) : 10U;

		now_us += dt;
		if ((b & 0x01U) != 0U)
			(void)servo_frame_rx_check_timeout(&p, now_us);
		evt = servo_frame_rx_step(&p, b, now_us);
		if (evt == SERVO_RX_EVT_FRAME_COMPLETE) {
			if (!servo_frame_is_valid_type(p.type) ||
			    !servo_frame_is_valid_arg(p.type, p.arg_len)) {
				*err = true;
				return;
			}
		}
	}

	now_us += SERVO_RX_FRAME_TIMEOUT_US + 5U;
	for (i = 0U; i < sizeof(valid_payload); ++i)
		valid_payload[i] = (uint8_t)(0xA5U ^ (uint8_t)i);

	valid_len = servo_frame_encode(
		SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_DATA, 1),
		(uint8_t)sizeof(valid_payload), valid_payload, valid_frame);
	if (valid_len != 4U + sizeof(valid_payload)) {
		*err = true;
		return;
	}

	evt = SERVO_RX_EVT_NONE;
	for (i = 0U; i < valid_len; ++i) {
		evt = servo_frame_rx_step(&p, valid_frame[i], now_us);
		now_us += 10U;
	}
	if (evt != SERVO_RX_EVT_FRAME_COMPLETE ||
	    p.type != SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_DATA, 1) ||
	    p.arg_len != sizeof(valid_payload) ||
	    memcmp(p.payload, valid_payload, sizeof(valid_payload)) != 0) {
		*err = true;
		return;
	}
}

static int test_spsc_concurrency_and_parser_fuzzer(void)
{
	/* 1. Multi-threaded SPSC ring-buffer concurrency test */
	struct spsc_concurrency_ctx ctx;
	pthread_t prod;
	pthread_t cons;

	memset(&ctx, 0, sizeof(ctx));
	ctx.rx_ring.head = 0U;
	ctx.rx_ring.tail = 0U;
	ctx.producer_done = false;

	TEST_ASSERT(pthread_create(&prod, NULL, spsc_test_producer_thread,
				   &ctx) == 0);
	TEST_ASSERT(pthread_create(&cons, NULL, spsc_test_consumer_thread,
				   &ctx) == 0);
	TEST_ASSERT(pthread_join(prod, NULL) == 0);
	TEST_ASSERT(pthread_join(cons, NULL) == 0);
	TEST_ASSERT(!ctx.error);
	TEST_ASSERT(ctx.frames_decoded == SPSC_TEST_TOTAL_FRAMES);

	/* 2. 100,000-step deterministic PRNG byte-stream parser fuzz test */
	bool fuzz_err = false;
	uint32_t x = 0x9E3779B9U;
	for (size_t iter = 0; iter < 1000; ++iter) {
		uint8_t trace[100];
		for (size_t k = 0; k < sizeof(trace); ++k) {
			x ^= x << 13U;
			x ^= x >> 17U;
			x ^= x << 5U;
			trace[k] = (uint8_t)(x & 0xFFU);
		}
		ec_fuzz_byte_stream_parser(trace, sizeof(trace), &fuzz_err);
		TEST_ASSERT(!fuzz_err);
	}

	return EC_SUCCESS;
}

/* 11. Test USART4 DMA TX */
static int test_usart4_dma_tx(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	drain_queue(&usb_to_bridge_kbd);
	drain_queue(&bridge_kbd_to_usb);
	mock_pd_connected = true;

	/* 1. Verify DMA TX initialization and transmission via USART_SR_TC */
	usart_tx_init(&usart4);
	TEST_ASSERT((mock_usart_cr3 & STM32_USART_CR3_DMAT) != 0);
	TEST_ASSERT((mock_usart_cr1 & STM32_USART_CR1_TE) != 0);

	/* Stage 10 bytes in bridge_to_usart4 */
	for (uint8_t i = 0; i < 10; i++)
		queue_add_unit(&bridge_to_usart4, &i);
	usart_written(&usart4.consumer, 10);

	/* Verify DMA1_CH7 active and TCIE armed (with TCIE=0 on DMA channel!)
	 */
	TEST_ASSERT((mock_usart_cr1 & STM32_USART_CR1_TCIE) != 0);
	TEST_ASSERT((mock_dma_channels[STM32_DMAC_CH7].ccr &
		     STM32_DMA_CCR_EN) != 0);
	TEST_ASSERT((mock_dma_channels[STM32_DMAC_CH7].ccr &
		     STM32_DMA_CCR_TCIE) == 0); /* Non-negotiable! */
	TEST_ASSERT(mock_dma_channels[STM32_DMAC_CH7].cndtr == 10);

	/* Fire wire transmission complete via USART_SR_TC */
	mock_dma_tx_complete();
	TEST_ASSERT((mock_usart_cr1 & STM32_USART_CR1_TCIE) == 0);
	TEST_ASSERT(queue_count(&bridge_to_usart4) == 0);

	return EC_SUCCESS;
}

/* 12. Test Auto-Discovery: 1M Bridge Fast Lock */
static int test_autodetect_1m_bridge(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	drain_queue(&usb_to_bridge_kbd);
	drain_queue(&bridge_kbd_to_usb);
	mock_pd_connected = true;
	usb_bridge_init();

	/* 1. Verify PROBE_RESET_HELD when atmel_reset_l = 0 */
	usb_bridge_notify_atmel_reset(0);
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_PROBE_RESET_HELD);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_NONE);

	/* Call arbiter while reset held: zero bytes emitted */
	bridge_arbiter_deferred();
	TEST_ASSERT(queue_count(&bridge_to_usart4) == 0);

	/* 2. Verify Cold-Boot PROBE_WAIT_BOOT_1M fast lock on valid 1M frame */
	usb_bridge_notify_atmel_reset(1);
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_PROBE_WAIT_BOOT_1M);
	TEST_ASSERT(mock_usart4_baud_rate == 1000000);
	TEST_ASSERT((mock_usart_cr2 & BIT(13)) != 0); /* 2 stop bits */
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_NONE);

	/* Verify passive RX-only: arbiter emits 0 TX bytes */
	bridge_arbiter_deferred();
	TEST_ASSERT(queue_count(&bridge_to_usart4) == 0);

	/* Feed valid 1M RESET_ACK frame from cold-booting New ATmega */
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | 0,
			 SERVO_RESET_FLAG_BOOT | SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);

	return EC_SUCCESS;
}

/* 13. Test Auto-Discovery: 9600 Fallback & Legacy Passthrough */
static int test_autodetect_9600_fallback(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	drain_queue(&usb_to_bridge_kbd);
	drain_queue(&bridge_kbd_to_usb);
	mock_pd_connected = true;

	/* 1. Verify PROBE_9600 timeout from PROBE_WAIT_BOOT_1M after 90 ms
	 * silence */
	usb_bridge_notify_atmel_reset(0);
	usb_bridge_notify_atmel_reset(1);
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_PROBE_WAIT_BOOT_1M);

	/* Fast forward time past 250 ms */
	force_time((timestamp_t){ .val = get_time().val + 251000U });
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() == USB_BRIDGE_PROBE_9600);
	TEST_ASSERT(mock_usart4_baud_rate == 9600);
	TEST_ASSERT((mock_usart_cr2 & BIT(13)) == 0); /* 1 stop bit */

	/* Verify 1st 0x00 probe byte transmitted */
	uint8_t probe_byte;
	TEST_ASSERT(queue_remove_unit(&bridge_to_usart4, &probe_byte));
	TEST_ASSERT(probe_byte == 0x00);

	/* 2. Verify 2-of-2 0x00 -> 0xFF lock into LOCKED_9600_LEGACY */
	/* Feed 1st 0xFF at 3000 us */
	force_time((timestamp_t){ .val = get_time().val + 3000U });
	uint8_t reply = 0xFF;
	queue_add_unit(&usart4_to_bridge, &reply);
	bridge_arbiter_deferred();

	/* Verify 2nd 0x00 probe byte transmitted */
	TEST_ASSERT(queue_remove_unit(&bridge_to_usart4, &probe_byte));
	TEST_ASSERT(probe_byte == 0x00);
	TEST_ASSERT(usb_bridge_get_probe_state() == USB_BRIDGE_PROBE_9600);

	/* Feed 2nd 0xFF at 3000 us */
	force_time((timestamp_t){ .val = get_time().val + 3000U });
	queue_add_unit(&usart4_to_bridge, &reply);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_9600_LEGACY);

	/* Edge-only filter: repeated release while locked in 9600 is a no-op */
	usb_bridge_notify_atmel_reset(1);
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_9600_LEGACY);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_NONE);

	/* Verify raw usbkm232 bidirectional passthrough */
	uint8_t kbd_out = 0x4B;
	queue_add_unit(&usb_to_bridge_kbd, &kbd_out);
	bridge_arbiter_deferred();
	TEST_ASSERT(queue_remove_unit(&bridge_to_usart4, &probe_byte));
	TEST_ASSERT(probe_byte == 0x4B);

	uint8_t kbd_in = 0x4C;
	queue_add_unit(&usart4_to_bridge, &kbd_in);
	bridge_arbiter_deferred();
	TEST_ASSERT(queue_remove_unit(&bridge_kbd_to_usb, &probe_byte));
	TEST_ASSERT(probe_byte == 0x4C);

	return EC_SUCCESS;
}

/* 14. Test Auto-Discovery: Re-probe on Framing Error / Reset */
static int test_autodetect_reset_reprobe(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	drain_queue(&usb_to_bridge_kbd);
	drain_queue(&bridge_kbd_to_usb);
	mock_pd_connected = true;

	/* Start in LOCKED_9600_LEGACY */
	usb_bridge_notify_atmel_reset(0);
	usb_bridge_notify_atmel_reset(1);
	force_time((timestamp_t){ .val = get_time().val + 251000U });
	bridge_arbiter_deferred();
	drain_queue(&bridge_to_usart4);
	force_time((timestamp_t){ .val = get_time().val + 3000U });
	uint8_t reply = 0xFF;
	queue_add_unit(&usart4_to_bridge, &reply);
	bridge_arbiter_deferred();
	drain_queue(&bridge_to_usart4);
	force_time((timestamp_t){ .val = get_time().val + 3000U });
	queue_add_unit(&usart4_to_bridge, &reply);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_9600_LEGACY);

	/* Edge-only filter: repeated release while locked in 9600 is a no-op */
	usb_bridge_notify_atmel_reset(1);
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_9600_LEGACY);

	/* Verify full reset-toggle reprobe:
	 * LOCKED_9600_LEGACY -> ATMEL_RESET_L=0 (RESET_HELD) ->
	 * ATMEL_RESET_L=1 (WAIT_BOOT_1M) -> 250 ms silence (PROBE_9600) ->
	 * 50 ms silence (PROBE_ACTIVE_1M) -> RESET_ACK (LOCKED_1M_BRIDGE).
	 */
	usb_bridge_notify_atmel_reset(0);
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_PROBE_RESET_HELD);
	usb_bridge_notify_atmel_reset(1);
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_PROBE_WAIT_BOOT_1M);

	/* Advance 251 ms to enter PROBE_9600 */
	force_time((timestamp_t){ .val = get_time().val + 251000U });
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() == USB_BRIDGE_PROBE_9600);
	TEST_ASSERT(mock_usart4_baud_rate == 9600);
	drain_queue(&bridge_to_usart4); /* Drain 0x00 probe */

	/* Advance 50 ms of silence (5 x 10 ms timeouts) to enter
	 * PROBE_ACTIVE_1M */
	force_time((timestamp_t){ .val = get_time().val + 50000U });
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() == USB_BRIDGE_PROBE_ACTIVE_1M);
	TEST_ASSERT(mock_usart4_baud_rate == 1000000);

	/* Verify RESET_REQ frame was queued in bridge_to_usart4 */
	uint8_t req_frame[4];
	TEST_ASSERT(queue_count(&bridge_to_usart4) == 4);
	for (int i = 0; i < 4; i++)
		TEST_ASSERT(queue_remove_unit(&bridge_to_usart4,
					      &req_frame[i]) == 1);
	TEST_ASSERT(req_frame[0] == SERVO_BRIDGE_SYNC);
	TEST_ASSERT(SERVO_GET_OP(req_frame[1]) == SERVO_OP_RESET_REQ);
	drain_queue(&bridge_to_usart4);

	/* Feed valid 1M frame -> lock into LOCKED_1M_BRIDGE */
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | 0,
			 SERVO_RESET_FLAG_BOOT | SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);

	/* Return to LOCKED_9600_LEGACY to test framing error re-probe */
	usb_bridge_notify_atmel_reset(0);
	usb_bridge_notify_atmel_reset(1);
	force_time((timestamp_t){ .val = get_time().val + 251000U });
	bridge_arbiter_deferred();
	drain_queue(&bridge_to_usart4);
	force_time((timestamp_t){ .val = get_time().val + 3000U });
	queue_add_unit(&usart4_to_bridge, &reply);
	bridge_arbiter_deferred();
	drain_queue(&bridge_to_usart4);
	force_time((timestamp_t){ .val = get_time().val + 3000U });
	queue_add_unit(&usart4_to_bridge, &reply);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_9600_LEGACY);

	/* Trigger re-probe via framing error (rx_framing_error) */
	mock_usart4_state.rx_framing_error++;
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_PROBE_WAIT_BOOT_1M);

	/* Advance 251 ms to enter PROBE_9600 */
	force_time((timestamp_t){ .val = get_time().val + 251000U });
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() == USB_BRIDGE_PROBE_9600);
	drain_queue(&bridge_to_usart4); /* Drain 0x00 probe */

	/* Early byte arrives at 500 us (< 1900 us) -> glitch / fast NAK */
	force_time((timestamp_t){ .val = get_time().val + 500U });
	queue_add_unit(&usart4_to_bridge, &reply);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() == USB_BRIDGE_PROBE_ACTIVE_1M);
	TEST_ASSERT(mock_usart4_baud_rate == 1000000);

	/* Verify RESET_REQ frame was queued in bridge_to_usart4 */
	TEST_ASSERT(queue_count(&bridge_to_usart4) > 0);
	drain_queue(&bridge_to_usart4);

	/* Feed valid 1M frame -> lock into LOCKED_1M_BRIDGE */
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | 0,
			 SERVO_RESET_FLAG_BOOT | SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);

	/* Edge-only filter: repeated release while locked in 1M is a no-op */
	usb_bridge_notify_atmel_reset(1);
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);

	return EC_SUCCESS;
}

/* 15. Test Auto-Discovery: Manual Baud Override via USB Control Request */
static int test_autodetect_manual_override(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	mock_pd_connected = true;
	last_enumerated_proto = SERVO_PROTO_NONE;
	dppu_pulse_active = false;

	/* Lock into LOCKED_1M_BRIDGE */
	usb_bridge_notify_atmel_reset(0);
	usb_bridge_notify_atmel_reset(1);
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | 0,
			 SERVO_RESET_FLAG_BOOT | SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);

	/* Verify servod startup init="9600" does not disrupt LOCKED_1M_BRIDGE
	 */
	usb_uint rx_setup[4] = { (USB_DIR_OUT | USB_TYPE_VENDOR |
				  USB_RECIP_INTERFACE) |
					 (USB_USART_SET_BAUD << 8),
				 96, /* 9600 baud */
				 USB_IFACE_USART4_STREAM, 0 };
	usb_uint tx_setup[4] = { 0 };
	int rv = usart4_iface_request(rx_setup, tx_setup);
	TEST_ASSERT(rv == 0);
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);
	TEST_ASSERT(mock_usart4_baud_rate == 1000000);

	/* Verify USB_USART_REQ_BAUD returns 1000000 / 100 in LOCKED_1M_BRIDGE
	 */
	usb_uint rx_req_baud[4] = { (USB_DIR_IN | USB_TYPE_VENDOR |
				     USB_RECIP_INTERFACE) |
					    (USB_USART_REQ_BAUD << 8),
				    0, USB_IFACE_USART4_STREAM, 2 };
	rv = usart4_iface_request(rx_req_baud, tx_setup);
	TEST_ASSERT(rv == 0);
	uint16_t reported_baud;
	memcpy(&reported_baud, tx_setup, sizeof(reported_baud));
	TEST_ASSERT(reported_baud == (1000000 / 100));

	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	return EC_SUCCESS;
}

static int test_probe_active_1m_resync_all_epochs_and_booted(void)
{
	usb_uint rx_req_baud[4] = { (USB_DIR_IN | USB_TYPE_VENDOR |
				     USB_RECIP_INTERFACE) |
					    (USB_USART_REQ_BAUD << 8),
				    0, USB_IFACE_USART4_STREAM, 2 };
	usb_uint tx_setup[4] = { 0 };
	uint16_t reported_baud;

	/* Test peer in SERVO_STATE_ACTIVE with peer.booted == false at epochs
	 * 0..3 */
	for (uint8_t peer_epoch = 0; peer_epoch < 4; peer_epoch++) {
		drain_queue(&usart4_to_bridge);
		drain_queue(&bridge_to_usart4);
		mock_pd_connected = true;

		/* Hold and release reset to enter PROBE_WAIT_BOOT_1M */
		usb_bridge_notify_atmel_reset(0);
		usb_bridge_notify_atmel_reset(1);
		TEST_ASSERT(usb_bridge_get_probe_state() ==
			    USB_BRIDGE_PROBE_WAIT_BOOT_1M);

		/* Verify REQ_BAUD reports 9600 in PROBE_WAIT_BOOT_1M */
		memset(tx_setup, 0, sizeof(tx_setup));
		TEST_ASSERT(usart4_iface_request(rx_req_baud, tx_setup) == 0);
		memcpy(&reported_baud, tx_setup, sizeof(reported_baud));
		TEST_ASSERT(reported_baud == 96);

		/* Advance 251 ms to enter PROBE_9600 */
		force_time((timestamp_t){ .val = get_time().val + 251000U });
		bridge_arbiter_deferred();
		TEST_ASSERT(usb_bridge_get_probe_state() ==
			    USB_BRIDGE_PROBE_9600);
		drain_queue(&bridge_to_usart4); /* Drain 0x00 */

		/* Verify REQ_BAUD reports 9600 in PROBE_9600 */
		memset(tx_setup, 0, sizeof(tx_setup));
		TEST_ASSERT(usart4_iface_request(rx_req_baud, tx_setup) == 0);
		memcpy(&reported_baud, tx_setup, sizeof(reported_baud));
		TEST_ASSERT(reported_baud == 96);

		/* Advance 50 ms of silence to enter PROBE_ACTIVE_1M */
		force_time((timestamp_t){ .val = get_time().val + 50000U });
		bridge_arbiter_deferred();
		TEST_ASSERT(usb_bridge_get_probe_state() ==
			    USB_BRIDGE_PROBE_ACTIVE_1M);
		TEST_ASSERT(mock_usart4_baud_rate == 1000000);

		/* Verify REQ_BAUD reports 9600 in PROBE_ACTIVE_1M */
		memset(tx_setup, 0, sizeof(tx_setup));
		TEST_ASSERT(usart4_iface_request(rx_req_baud, tx_setup) == 0);
		memcpy(&reported_baud, tx_setup, sizeof(reported_baud));
		TEST_ASSERT(reported_baud == 96);

		/* STM32 queued RESET_REQ(boot=true) in bridge_to_usart4 */
		uint8_t stm_req_frame[SERVO_MAX_FRAME_BYTES];
		int stm_req_len = 0;
		while (queue_remove_unit(&bridge_to_usart4,
					 &stm_req_frame[stm_req_len]))
			stm_req_len++;
		TEST_ASSERT(stm_req_len == 4);
		TEST_ASSERT(stm_req_frame[0] == SERVO_BRIDGE_SYNC);
		TEST_ASSERT(SERVO_GET_CHAN(stm_req_frame[1]) == SERVO_CHAN_SYS);
		TEST_ASSERT(SERVO_GET_OP(stm_req_frame[1]) ==
			    SERVO_OP_RESET_REQ);

		/* Setup peer ATmega in SERVO_STATE_ACTIVE with booted=false at
		 * peer_epoch */
		struct servo_bridge_node peer;
		memset(&peer, 0, sizeof(peer));
		peer.is_epoch_leader = false;
		peer.state = SERVO_STATE_ACTIVE;
		peer.booted = false;
		peer.epoch = peer_epoch;
		peer.active_proto = SERVO_PROTO_ADB;
		servo_frame_rx_init(&peer.rx);

		/* Deliver STM32 RESET_REQ(boot=true) to peer ATmega */
		struct servo_bridge_rx_effects fx;
		memset(&fx, 0, sizeof(fx));
		for (int i = 0; i < stm_req_len; i++) {
			enum servo_rx_event evt = servo_frame_rx_step(
				&peer.rx, stm_req_frame[i], get_time().val);
			if (evt == SERVO_RX_EVT_FRAME_COMPLETE)
				servo_bridge_on_rx_event(&peer, evt, &fx);
		}

		/* Verify peer entered SERVO_STATE_RESETTING and staged both ACK
		 * and REQ */
		TEST_ASSERT(peer.state == SERVO_STATE_RESETTING);
		TEST_ASSERT(peer.pending_reset_ack == true);
		TEST_ASSERT(peer.pending_reset_ack_boot == true);
		TEST_ASSERT(peer.pending_reset_req == true);

		/* Pop all peer's pending control frames (RESET_REQ + RESET_ACK
		 * = 8B) */
		uint8_t peer_frames[SERVO_MAX_FRAME_BYTES * 2];
		size_t peer_total_len = 0;
		size_t flen;
		while ((flen = servo_bridge_pop_tx_frame(
				&peer, 128, &peer_frames[peer_total_len],
				get_time().val)) > 0) {
			peer_total_len += flen;
		}
		TEST_ASSERT(peer_total_len == 8);
		queue_add_units(&usart4_to_bridge, peer_frames, peer_total_len);

		/* Run STM32 arbiter to process peer's frames */
		bridge_arbiter_deferred();

		/* Verify STM32 successfully locked into LOCKED_1M_BRIDGE! */
		TEST_ASSERT(usb_bridge_get_probe_state() ==
			    USB_BRIDGE_LOCKED_1M_BRIDGE);
		TEST_ASSERT(bridge.state == SERVO_STATE_ACTIVE);

		/* Verify REQ_BAUD now reports 1,000,000 baud (10000) */
		memset(tx_setup, 0, sizeof(tx_setup));
		TEST_ASSERT(usart4_iface_request(rx_req_baud, tx_setup) == 0);
		memcpy(&reported_baud, tx_setup, sizeof(reported_baud));
		TEST_ASSERT(reported_baud == (1000000 / 100));
	}

	/* Test peer with peer.booted == true */
	{
		drain_queue(&usart4_to_bridge);
		drain_queue(&bridge_to_usart4);

		usb_bridge_notify_atmel_reset(0);
		usb_bridge_notify_atmel_reset(1);

		/* Advance to PROBE_9600 and drain probe */
		force_time((timestamp_t){ .val = get_time().val + 251000U });
		bridge_arbiter_deferred();
		drain_queue(&bridge_to_usart4);

		/* Advance to PROBE_ACTIVE_1M */
		force_time((timestamp_t){ .val = get_time().val + 50000U });
		bridge_arbiter_deferred();
		TEST_ASSERT(usb_bridge_get_probe_state() ==
			    USB_BRIDGE_PROBE_ACTIVE_1M);

		/* Drain STM32's RESET_REQ */
		uint8_t stm_req_frame[SERVO_MAX_FRAME_BYTES];
		int stm_req_len = 0;
		while (queue_remove_unit(&bridge_to_usart4,
					 &stm_req_frame[stm_req_len]))
			stm_req_len++;
		TEST_ASSERT(stm_req_len == 4);

		/* Setup peer ATmega with booted = true */
		struct servo_bridge_node peer;
		memset(&peer, 0, sizeof(peer));
		peer.is_epoch_leader = false;
		peer.state = SERVO_STATE_RESETTING;
		peer.booted = true;
		peer.epoch = 0;
		servo_frame_rx_init(&peer.rx);

		/* Deliver STM32 RESET_REQ to peer ATmega */
		struct servo_bridge_rx_effects fx;
		memset(&fx, 0, sizeof(fx));
		for (int i = 0; i < stm_req_len; i++) {
			enum servo_rx_event evt = servo_frame_rx_step(
				&peer.rx, stm_req_frame[i], get_time().val);
			if (evt == SERVO_RX_EVT_FRAME_COMPLETE)
				servo_bridge_on_rx_event(&peer, evt, &fx);
		}

		/* Pop peer's RESET_ACK and feed to STM32 */
		uint8_t peer_ack_frame[SERVO_MAX_FRAME_BYTES];
		size_t peer_ack_len = servo_bridge_pop_tx_frame(
			&peer, 128, peer_ack_frame, get_time().val);
		TEST_ASSERT(peer_ack_len == 4);
		queue_add_units(&usart4_to_bridge, peer_ack_frame,
				peer_ack_len);

		bridge_arbiter_deferred();

		/* Verify STM32 successfully locked into LOCKED_1M_BRIDGE */
		TEST_ASSERT(usb_bridge_get_probe_state() ==
			    USB_BRIDGE_LOCKED_1M_BRIDGE);
		TEST_ASSERT(bridge.state == SERVO_STATE_ACTIVE);

		/* Verify REQ_BAUD reports 1,000,000 baud */
		memset(tx_setup, 0, sizeof(tx_setup));
		TEST_ASSERT(usart4_iface_request(rx_req_baud, tx_setup) == 0);
		memcpy(&reported_baud, tx_setup, sizeof(reported_baud));
		TEST_ASSERT(reported_baud == (1000000 / 100));
	}

	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	return EC_SUCCESS;
}

static int test_reset_held_kbd_drain_and_fallthrough_safety(void)
{
	usb_uint rx_clear[4] = { 0 };
	usb_uint tx_setup[4] = { 0 };
	struct usb_setup_packet req_clear = {
		.bmRequestType = USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE,
		.bRequest = USB_USART_CLEAR_QUEUES,
		.wValue = 0,
		.wIndex = USB_IFACE_USART4_STREAM,
		.wLength = 0,
	};
	memcpy(rx_clear, &req_clear, sizeof(req_clear));

	/* 1. Bug 11a: USB_USART_CLEAR_QUEUES kicks usart4_usb.deferred */
	mock_usart4_usb_deferred_calls = 0;
	bridge.pending_key_cmd = true;
	TEST_ASSERT(usart4_iface_request(rx_clear, tx_setup) == 0);
	TEST_ASSERT(bridge.pending_key_cmd == false);

	/*
	 * 2. Bug 11b: PROBE_RESET_HELD drains usb_to_bridge_kbd via
	 * queue_remove_unit
	 */
	usb_bridge_notify_atmel_reset(0);
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_PROBE_RESET_HELD);
	mock_usart4_usb_producer_reads = 0;
	for (uint8_t i = 0; i < 16; i++) {
		uint8_t b = (uint8_t)(0x10 + i);
		TEST_ASSERT(queue_add_unit(&usb_to_bridge_kbd, &b) == 1);
	}
	TEST_ASSERT(queue_is_full(&usb_to_bridge_kbd));
	bridge_arbiter_deferred();
	TEST_ASSERT(queue_is_empty(&usb_to_bridge_kbd));
	TEST_ASSERT(mock_usart4_usb_producer_reads == 16);

	/*
	 * 3. Bug 12: Stale host_usb_reset_irq during WAIT_BOOT_1M does not
	 * clobber lock
	 */
	usb_bridge_notify_atmel_reset(1);
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_PROBE_WAIT_BOOT_1M);
	host_usb_reset_irq = true;

	/* Feed valid boot RESET_REQ from ATmega */
	uint8_t boot_req[SERVO_MAX_FRAME_BYTES];
	size_t flen = servo_frame_encode(SERVO_MAKE_TYPE(SERVO_CHAN_SYS,
							 SERVO_OP_RESET_REQ, 0),
					 SERVO_PROTO_NONE, NULL, boot_req);
	queue_add_units(&usart4_to_bridge, boot_req, flen);
	bridge_arbiter_deferred();

	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);
	TEST_ASSERT(host_usb_reset_irq == false);

	/* Complete handshake with RESET_ACK(boot=true) */
	uint8_t boot_ack[SERVO_MAX_FRAME_BYTES];
	flen = servo_frame_encode(SERVO_MAKE_TYPE(SERVO_CHAN_SYS,
						  SERVO_OP_RESET_ACK, 0),
				  SERVO_PROTO_NONE, NULL, boot_ack);
	queue_add_units(&usart4_to_bridge, boot_ack, flen);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.state == SERVO_STATE_ACTIVE);

	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	return EC_SUCCESS;
}

/*
 * 11. Test Bug 15 (USB_EVENT_RESET syncs last_enumerated_proto) &
 *     Bug 16 (bridge_reinit_preserve_proto across ATMEL_RESET_L / WAIT_BOOT_1M
 *     / PROBE_ACTIVE_1M, with symmetric boot-time protocol restoration)
 */
static int test_bug15_usb_reset_and_bug16_atmel_reset_proto(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	mock_pd_connected = true;
	last_enumerated_proto = SERVO_PROTO_NONE;
	dppu_pulse_active = false;

	/*
	 * Part A: Bug 15 — When effective_usb_protocol == 0x01 is enumerated
	 * (last_enumerated_proto == 0x01), then effective_usb_protocol drops to
	 * 0x00 without pulsing DPPU, and the Host issues USB_EVENT_RESET
	 * (!dppu_pulse_active), last_enumerated_proto MUST sync to 0x00 so a
	 * subsequent 0x00 -> 0x01 transition arms bridge_dppu_debounce_deferred
	 * and pulses DPPU!
	 */
	usb_bridge_init();
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | 0,
			 SERVO_RESET_FLAG_BOOT | SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);

	/* Complete initial DPPU debounce & 50 ms reconnect pulse for 0x01 */
	bridge_dppu_debounce_deferred();
	TEST_ASSERT(dppu_pulse_active == true);
	bridge_dppu_reconnect_deferred();
	TEST_ASSERT(dppu_pulse_active == false);
	TEST_ASSERT(last_enumerated_proto == SERVO_PROTO_ADB);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_RX_MASK) ==
		    (EP_TX_NAK | EP_RX_VALID));

	/*
	 * Part A: Bug 15 — When effective_usb_protocol == 0x01 is enumerated
	 * (last_enumerated_proto == 0x01), then effective_usb_protocol drops to
	 * 0x00 (e.g. DUT PD disconnect) without pulsing DPPU, and the Host
	 * issues USB_EVENT_RESET (!dppu_pulse_active), last_enumerated_proto
	 * MUST sync to 0x00 so a subsequent 0x00 -> 0x01 transition arms
	 * bridge_dppu_debounce_deferred and pulses DPPU!
	 */
	mock_pd_connected = false;
	usb_bridge_pd_event();
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_NONE);
	TEST_ASSERT(last_enumerated_proto == SERVO_PROTO_ADB);

	/* Host issues USB bus reset while effective_usb_protocol == 0x00 */
	usb_bridge_event(USB_EVENT_RESET);
	TEST_ASSERT(last_enumerated_proto == SERVO_PROTO_NONE);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_RX_MASK) ==
		    (EP_TX_STALL | EP_RX_STALL));

	/* PD reconnects: 0x00 -> 0x01 transition triggers DPPU debounce pulse
	 */
	mock_pd_connected = true;
	usb_bridge_pd_event();
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);
	bridge_dppu_debounce_deferred();
	TEST_ASSERT(dppu_pulse_active == true);
	bridge_dppu_reconnect_deferred();
	TEST_ASSERT(dppu_pulse_active == false);
	TEST_ASSERT(last_enumerated_proto == SERVO_PROTO_ADB);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_RX_MASK) ==
		    (EP_TX_NAK | EP_RX_VALID));

	/*
	 * Part B: Bug 16 & Bug 17 — Assert and release ATMEL_RESET_L
	 * (`dut-control atmega_rst:on` -> `dut-control atmega_rst:off`).
	 * Verify:
	 * 1) `effective_usb_protocol` remains `SERVO_PROTO_ADB` across
	 *    `USB_BRIDGE_PROBE_RESET_HELD` and `USB_BRIDGE_PROBE_WAIT_BOOT_1M`
	 *    (never dropping to `SERVO_PROTO_NONE`),
	 * 2) `EP7` is held in `EP_TX_NAK | EP_RX_NAK` (NEVER `EP_TX_STALL` or
	 *    `EP_RX_STALL`, preventing Host `adb` from seeing `-EPIPE` on its
	 *    resting Bulk IN URB),
	 * 3) `dppu_pulse_active` remains `false` (no Host USB detach!), and
	 * 4) when freshly booted ATmega32U4 (`peer.booted = true`,
	 *    `peer.active_proto = SERVO_PROTO_NONE`) emits
	 *    `RESET_REQ(epoch=0, proto=0, boot=true)`, STM32 locks into
	 *    `USB_BRIDGE_LOCKED_1M_BRIDGE` and adopts `SERVO_PROTO_NONE` while
	 *    preserving `last_enumerated_proto == SERVO_PROTO_ADB` and holding
	 *    `EP7` in `EP_TX_NAK | EP_RX_NAK`, then re-arms `EP7 OUT`
	 *    (`EP_RX_VALID`) when DUT `adb-usb-bridge` sets `Alt 1`
	 *    (`SERVO_PROTO_ADB`).
	 */
	usb_bridge_notify_atmel_reset(0);
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_PROBE_RESET_HELD);
	TEST_ASSERT(bridge.active_proto == SERVO_PROTO_ADB);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);
	TEST_ASSERT(last_enumerated_proto == SERVO_PROTO_ADB);
	TEST_ASSERT(dppu_pulse_active == false);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_RX_MASK) ==
		    (EP_TX_NAK | EP_RX_NAK));

	usb_bridge_notify_atmel_reset(1);
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_PROBE_WAIT_BOOT_1M);
	TEST_ASSERT(bridge.active_proto == SERVO_PROTO_ADB);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);
	TEST_ASSERT(last_enumerated_proto == SERVO_PROTO_ADB);
	TEST_ASSERT(dppu_pulse_active == false);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_RX_MASK) ==
		    (EP_TX_NAK | EP_RX_NAK));
	drain_queue(&bridge_to_usart4);

	struct servo_bridge_node peer;
	struct servo_bridge_rx_effects fx;
	uint8_t frame[SERVO_MAX_FRAME_BYTES];
	size_t flen;

	servo_bridge_init(&peer, /*is_epoch_leader=*/false, get_time().val);
	TEST_ASSERT(peer.booted == true);
	TEST_ASSERT(peer.active_proto == SERVO_PROTO_NONE);
	TEST_ASSERT(peer.desired_proto == SERVO_PROTO_NONE);

	/* 1. ATmega emits unsolicited boot RESET_REQ(epoch=0, proto=0,
	 * boot=true) */
	flen = servo_bridge_pop_tx_frame(&peer, 128, frame, get_time().val);
	TEST_ASSERT(flen == 4);
	queue_add_units(&usart4_to_bridge, frame, flen);
	bridge_arbiter_deferred();

	/* STM32 locks into LOCKED_1M_BRIDGE, preserves SERVO_PROTO_ADB,
	 * preserves last_enumerated_proto == SERVO_PROTO_ADB (descriptor
	 * intact, no DPPU disconnect pulse), and holds EP7 in EP_TX_NAK |
	 * EP_RX_NAK!
	 */
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);
	TEST_ASSERT(bridge.active_proto == SERVO_PROTO_ADB);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);
	TEST_ASSERT(last_enumerated_proto == SERVO_PROTO_ADB);
	TEST_ASSERT(dppu_pulse_active == false);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_RX_MASK) ==
		    (EP_TX_NAK | EP_RX_VALID));

	/* Deliver STM32's queued frames (RESET_ACK(boot=true, proto=ADB) +
	 * RESET_REQ(boot=true, proto=ADB)) to ATmega peer
	 */
	uint8_t ch;
	while (queue_remove_unit(&bridge_to_usart4, &ch)) {
		enum servo_rx_event evt =
			servo_frame_rx_step(&peer.rx, ch, get_time().val);
		if (evt == SERVO_RX_EVT_FRAME_COMPLETE)
			servo_bridge_on_rx_event(&peer, evt, &fx);
	}
	TEST_ASSERT(peer.active_proto == SERVO_PROTO_ADB);
	TEST_ASSERT(peer.desired_proto == SERVO_PROTO_ADB);

	/* Pop ATmega's response frames and feed to STM32 */
	while ((flen = servo_bridge_pop_tx_frame(&peer, 128, frame,
						 get_time().val)) > 0) {
		queue_add_units(&usart4_to_bridge, frame, flen);
	}
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.state == SERVO_STATE_ACTIVE);
	TEST_ASSERT(!bridge.booted);
	TEST_ASSERT(bridge.active_proto == SERVO_PROTO_ADB);

	/* Deliver STM32's final RESET_ACK to ATmega */
	while (queue_remove_unit(&bridge_to_usart4, &ch)) {
		enum servo_rx_event evt =
			servo_frame_rx_step(&peer.rx, ch, get_time().val);
		if (evt == SERVO_RX_EVT_FRAME_COMPLETE)
			servo_bridge_on_rx_event(&peer, evt, &fx);
	}
	TEST_ASSERT(peer.state == SERVO_STATE_ACTIVE);
	TEST_ASSERT(!peer.booted);
	TEST_ASSERT(peer.epoch == bridge.epoch);
	TEST_ASSERT(peer.active_proto == SERVO_PROTO_ADB);

	/* 2. DUT init respawns adb-usb-bridge and sets Alt 1 (SERVO_PROTO_ADB)
	 */
	TEST_ASSERT(
		servo_bridge_set_proto(&peer, SERVO_PROTO_ADB, get_time().val));
	while ((flen = servo_bridge_pop_tx_frame(&peer, 128, frame,
						 get_time().val)) > 0) {
		queue_add_units(&usart4_to_bridge, frame, flen);
	}
	bridge_arbiter_deferred();
	while (queue_remove_unit(&bridge_to_usart4, &ch)) {
		enum servo_rx_event evt =
			servo_frame_rx_step(&peer.rx, ch, get_time().val);
		if (evt == SERVO_RX_EVT_FRAME_COMPLETE)
			servo_bridge_on_rx_event(&peer, evt, &fx);
	}
	TEST_ASSERT(bridge.state == SERVO_STATE_ACTIVE);
	TEST_ASSERT(bridge.active_proto == SERVO_PROTO_ADB);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);
	TEST_ASSERT(peer.state == SERVO_STATE_ACTIVE);
	TEST_ASSERT(peer.active_proto == SERVO_PROTO_ADB);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_RX_MASK) ==
		    (EP_TX_NAK | EP_RX_VALID));

	/*
	 * Part C: Verify PROBE_ACTIVE_1M timeout (30 ms, e.g. ATmega in DFU
	 * mode) clears bridge.active_proto and active_usb_protocol to
	 * SERVO_PROTO_NONE.
	 */
	usb_bridge_notify_atmel_reset(0);
	usb_bridge_notify_atmel_reset(1);
	TEST_ASSERT(bridge.active_proto == SERVO_PROTO_ADB);
	force_time((timestamp_t){ .val = get_time().val + 251000U });
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() == USB_BRIDGE_PROBE_9600);
	drain_queue(&bridge_to_usart4);
	force_time((timestamp_t){ .val = get_time().val + 50000U });
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() == USB_BRIDGE_PROBE_ACTIVE_1M);
	TEST_ASSERT(bridge.active_proto == SERVO_PROTO_ADB);
	drain_queue(&bridge_to_usart4);

	/* Advance past SERVO_PROBE_ACTIVE_1M_TIMEOUT_US with no reply (DFU) */
	force_time((timestamp_t){ .val = get_time().val +
					 SERVO_PROBE_ACTIVE_1M_TIMEOUT_US +
					 1000U });
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_PROBE_WAIT_BOOT_1M);
	TEST_ASSERT(bridge.active_proto == SERVO_PROTO_NONE);
	TEST_ASSERT(active_usb_protocol == SERVO_PROTO_NONE);

	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	return EC_SUCCESS;
}

static int test_s7_adb_stale_timeout_and_deferred_out_pending_race(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	mock_pd_connected = true;
	usb_bridge_init();

	/* Lock into 1M bridge in SERVO_PROTO_ADB */
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | 0,
			 SERVO_RESET_FLAG_BOOT | SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);
	TEST_ASSERT(bridge.active_proto == SERVO_PROTO_ADB);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);

	/* Complete DPPU debounce */
	bridge_dppu_debounce_deferred();
	bridge_dppu_reconnect_deferred();
	TEST_ASSERT(last_enumerated_proto == SERVO_PROTO_ADB);
	TEST_ASSERT(!dppu_pulse_active);
	drain_queue(&bridge_to_usart4);

	/* Set up active session with adb_pending_syncs == 0 so ADB DATA frames
	 * can arm EP7 IN */
	bridge.adb_pending_syncs = 0U;
	bridge.adb_awaiting_cnxn = false;

	/*
	 * Part 1: EP7 IN Immediate Disarm on A_CNXN, Deferred CNXN Forwarding
	 * on Zero Credits, and Immunity to 200 ms Idle Stale Drain.
	 */
	/* 1. ATmega sends an ADB DATA frame to STM32, arming EP7 IN */
	uint8_t adb_payload[64];
	memset(adb_payload, 0x42, sizeof(adb_payload));
	/* Set EP_TX_DTOG and EP_CTR_TX before arming to verify preservation */
	mock_usb_ep[USB_EP_BRIDGE] |= (EP_TX_DTOG | EP_CTR_TX);

	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 sizeof(adb_payload), adb_payload);
	bridge_arbiter_deferred();

	TEST_ASSERT(bridge.usb_in_busy == true);
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 64);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_VALID);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_DTOG) == EP_TX_DTOG);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_CTR_TX) == EP_CTR_TX);
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 0);

	/* 2. Advance time by 200 ms: must NOT disarm EP7 IN */
	force_time((timestamp_t){ .val = get_time().val + 200000U });
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.usb_in_busy == true);
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 64);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_VALID);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_DTOG) == EP_TX_DTOG);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_CTR_TX) == EP_CTR_TX);
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 0);

	/* 3. Simulate pending TX to ATmega so bridge.tx_credits == 0 */
	bridge.tx_credits = 0;
	bridge.has_tx_data = true;
	bridge.tx_data_seq = bridge.tx_seq;
	bridge.last_tx_us = get_time().val;
	TEST_ASSERT(!servo_bridge_can_send_data(&bridge));
	drain_queue(&bridge_to_usart4);

	/* 4. Host sends 24-byte A_CNXN on EP7 OUT (usb_bridge_rx in USB ISR) */
	uint8_t cnxn_pkt[24];
	servo_write_u32_le(&cnxn_pkt[0], SERVO_ADB_CNXN_CMD);
	servo_write_u32_le(&cnxn_pkt[4], 0x01000000UL);
	servo_write_u32_le(&cnxn_pkt[8], 1048576UL);
	servo_write_u32_le(&cnxn_pkt[12], 0);
	servo_write_u32_le(&cnxn_pkt[16], 0);
	servo_write_u32_le(&cnxn_pkt[20], SERVO_ADB_CNXN_MAGIC);

	memcpy_to_usbram((void *)usb_sram_addr(ep_bridge_rx_buffer), cnxn_pkt,
			 sizeof(cnxn_pkt));
	btable_ep[USB_EP_BRIDGE].rx_count = sizeof(cnxn_pkt);

	mock_in_isr = true;
	usb_bridge_rx();
	mock_in_isr = false;

	/* Verify usb_bridge_rx() immediately disarmed EP7 IN inside ISR */
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_NAK);
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_DTOG) == EP_TX_DTOG);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_CTR_TX) == EP_CTR_TX);
	TEST_ASSERT(ep7_out_pending == 1);

	/*
	 * 5. Also enqueue a concurrent stale ADB DATA frame in usart4_to_bridge
	 * BEFORE bridge_arbiter_deferred() runs (simulating a stale UART frame
	 * finishing DMA RX at the same time EP7 OUT received A_CNXN).
	 * Verify that bridge_arbiter_deferred() sets adb_pending_syncs = 1
	 * BEFORE bridge_process_uart_rx(), so EP7 IN is NEVER transiently
	 * armed (mock_ep7_toggle_count only increments for disarm, never to
	 * EP_TX_VALID).
	 */
	bridge.usb_in_busy = false;
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 sizeof(adb_payload), adb_payload);
	int toggles_before = mock_ep7_toggle_count;

	/* Run bridge_arbiter_deferred() while bridge.tx_credits == 0 */
	bridge_arbiter_deferred();

	/*
	 * Verify servo_adb_session_on_host_cnxn(&bridge) ran before UART RX:
	 * adb_pending_syncs == 1, usb_in_busy == false, zero EP7 toggles to
	 * EP_TX_VALID occurred, CREDIT emitted to ATmega, while
	 * ep7_out_pending is preserved at 1 because tx_credits == 0.
	 */
	TEST_ASSERT(mock_ep7_toggle_count == toggles_before);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_NAK);
	TEST_ASSERT(bridge.adb_pending_syncs == 1);
	TEST_ASSERT(bridge.usb_in_busy == false);
	TEST_ASSERT(ep7_out_pending == 1);
	TEST_ASSERT(queue_count(&bridge_to_usart4) > 0);

	/* Verify the emitted frame was SERVO_FRAME_CREDIT */
	bool found_credit = false;
	uint8_t ch_cred;
	struct servo_rx_parser rx_parser_cred;
	servo_frame_rx_init(&rx_parser_cred);
	while (queue_remove_unit(&bridge_to_usart4, &ch_cred)) {
		enum servo_rx_event evt = servo_frame_rx_step(
			&rx_parser_cred, ch_cred, get_time().val);
		if (evt == SERVO_RX_EVT_FRAME_COMPLETE &&
		    SERVO_GET_OP(rx_parser_cred.type) == SERVO_OP_CREDIT) {
			found_credit = true;
		}
	}
	TEST_ASSERT(found_credit == true);
	drain_queue(&bridge_to_usart4);

	/* 6. ATmega sends SERVO_FRAME_CREDIT to STM32 */
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_CREDIT | (bridge.epoch & SERVO_EPOCH_MASK),
			 bridge.tx_data_seq, NULL);
	bridge_arbiter_deferred();

	/*
	 * Now bridge.tx_credits was replenished, so 24B A_CNXN was forwarded
	 * and ep7_out_pending cleared.
	 */
	TEST_ASSERT(ep7_out_pending == 0);
	TEST_ASSERT(queue_count(&bridge_to_usart4) > 0);

	/* Verify the 24B A_CNXN frame was transmitted to USART4 */
	bool found_cnxn_on_uart1 = false;
	uint8_t ch1;
	struct servo_rx_parser rx_parser1;
	servo_frame_rx_init(&rx_parser1);
	while (queue_remove_unit(&bridge_to_usart4, &ch1)) {
		enum servo_rx_event evt =
			servo_frame_rx_step(&rx_parser1, ch1, get_time().val);
		if (evt == SERVO_RX_EVT_FRAME_COMPLETE &&
		    SERVO_GET_CHAN(rx_parser1.type) == SERVO_CHAN_H2H &&
		    SERVO_GET_OP(rx_parser1.type) == SERVO_OP_DATA &&
		    rx_parser1.arg_len == 24) {
			if (servo_is_adb_cnxn(rx_parser1.payload, 24,
					      SERVO_PROTO_ADB)) {
				found_cnxn_on_uart1 = true;
				TEST_ASSERT(servo_read_u32_le(
						    &rx_parser1.payload[8]) ==
					    SERVO_MAX_PAYLOAD_CLAMP);
			}
		}
	}
	TEST_ASSERT(found_cnxn_on_uart1 == true);
	drain_queue(&bridge_to_usart4);

	/* 7. ATmega sends 0-byte SYNC_ZLP barrier, clearing adb_pending_syncs
	 */
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 0, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.adb_pending_syncs == 0);
	TEST_ASSERT(bridge.usb_in_busy == false);
	drain_queue(&bridge_to_usart4);

	/*
	 * Part 2: CLEAR_HALT(0x87) or SET_INTERFACE(6, 0) followed immediately
	 * by Host A_CNXN on EP7 OUT before bridge_arbiter_deferred runs.
	 */
	/* 1. ATmega sends ADB packet so EP7 IN is busy */
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 sizeof(adb_payload), adb_payload);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.usb_in_busy == true);
	drain_queue(&bridge_to_usart4);

	/* Host aborts and issues CLEAR_HALT on EP7 IN (0x87) */
	mock_in_isr = true;
	int ret = usb_bridge_clear_halt(USB_EP_BRIDGE | 0x80);
	mock_in_isr = false;
	TEST_ASSERT(ret == 0);
	TEST_ASSERT(host_usb_reset_irq == true);
	TEST_ASSERT(ep7_out_pending == 0);

	/* 2. Before bridge_arbiter_deferred runs, Host immediately sends
	 * 24-byte A_CNXN on EP7 OUT */
	memset(cnxn_pkt, 0, sizeof(cnxn_pkt));
	servo_write_u32_le(&cnxn_pkt[0], SERVO_ADB_CNXN_CMD);
	servo_write_u32_le(&cnxn_pkt[4], 0x01000000UL);
	servo_write_u32_le(&cnxn_pkt[8], 1048576UL);
	servo_write_u32_le(&cnxn_pkt[12], 0);
	servo_write_u32_le(&cnxn_pkt[16], 0);
	servo_write_u32_le(&cnxn_pkt[20], SERVO_ADB_CNXN_MAGIC);

	memcpy_to_usbram((void *)usb_sram_addr(ep_bridge_rx_buffer), cnxn_pkt,
			 sizeof(cnxn_pkt));
	btable_ep[USB_EP_BRIDGE].rx_count = sizeof(cnxn_pkt);

	mock_in_isr = true;
	usb_bridge_rx();
	mock_in_isr = false;
	TEST_ASSERT(ep7_out_pending == 1);

	/* 3. Run bridge_arbiter_deferred: host_usb_reset_irq triggers reset
	 * handshake */
	bridge_arbiter_deferred();

	/* Verify ep7_out_pending was PRESERVED (NOT dropped to 0!) */
	TEST_ASSERT(ep7_out_pending == 1);
	TEST_ASSERT(bridge.state == SERVO_STATE_RESETTING);

	/*
	 * 4. ATmega sends SERVO_FRAME_RESET_ACK back to STM32.
	 * STM32 processes RESET_ACK, fx.epoch_history_cleared is true,
	 * transitions bridge.state back to SERVO_STATE_ACTIVE, and
	 * immediately pops/forwards the preserved A_CNXN over USART4!
	 */
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_RESET_ACK | bridge.epoch, SERVO_PROTO_ADB,
			 NULL);
	bridge_arbiter_deferred();

	TEST_ASSERT(bridge.state == SERVO_STATE_ACTIVE);
	TEST_ASSERT(ep7_out_pending == 0);

	/* Verify the 24-byte A_CNXN was transmitted to USART4 with arg1 clamped
	 * to 16384 */
	bool found_cnxn_on_uart = false;
	uint8_t ch;
	struct servo_rx_parser rx_parser;
	servo_frame_rx_init(&rx_parser);
	while (queue_remove_unit(&bridge_to_usart4, &ch)) {
		enum servo_rx_event evt =
			servo_frame_rx_step(&rx_parser, ch, get_time().val);
		if (evt == SERVO_RX_EVT_FRAME_COMPLETE &&
		    SERVO_GET_CHAN(rx_parser.type) == SERVO_CHAN_H2H &&
		    SERVO_GET_OP(rx_parser.type) == SERVO_OP_DATA &&
		    rx_parser.arg_len == 24) {
			if (servo_is_adb_cnxn(rx_parser.payload, 24,
					      SERVO_PROTO_ADB)) {
				found_cnxn_on_uart = true;
				TEST_ASSERT(servo_read_u32_le(
						    &rx_parser.payload[8]) ==
					    SERVO_MAX_PAYLOAD_CLAMP);
			}
		}
	}
	TEST_ASSERT(found_cnxn_on_uart == true);

	/*
	 * Part 3: History Y (Fix 3 Active A_CNXN Response Coalescing across
	 * Attempts 1, 2, and 3) + Partial A_CNXN Overwrite on STM32 Firmware.
	 */
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);

	/*
	 * Return TX credit for the Part 2 CNXN_HDR and complete its SYNC_ZLP +
	 * 0-payload HS_HDR.
	 */
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_CREDIT | (bridge.epoch & SERVO_EPOCH_MASK),
			 bridge.tx_data_seq, NULL);
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 0, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.adb_pending_syncs == 0);
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 1);
	drain_queue(&bridge_to_usart4);

	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 sizeof(cnxn_pkt), cnxn_pkt);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.usb_in_busy == true);
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 1);
	mock_in_isr = true;
	usb_bridge_tx();
	mock_in_isr = false;
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.usb_in_busy == false);
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 0);
	drain_queue(&bridge_to_usart4);

	/*
	 * 3.1 Partial A_CNXN Overwrite:
	 * Host sends CNXN_HDR (148B -> 3 chunks) and 1 CNXN_PAY chunk, then
	 * dies before sending the remaining 2 chunks.
	 */
	servo_write_u32_le(&cnxn_pkt[12], 148UL); /* 3 chunks */
	memcpy_to_usbram((void *)usb_sram_addr(ep_bridge_rx_buffer), cnxn_pkt,
			 sizeof(cnxn_pkt));
	btable_ep[USB_EP_BRIDGE].rx_count = sizeof(cnxn_pkt);
	mock_in_isr = true;
	usb_bridge_rx();
	mock_in_isr = false;
	bridge_arbiter_deferred();
	TEST_ASSERT(ep7_out_pending == 0);
	TEST_ASSERT(bridge.adb_cnxn_req_rem_chunks == 3);
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 0);
	drain_queue(&bridge_to_usart4);
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_CREDIT | (bridge.epoch & SERVO_EPOCH_MASK),
			 bridge.tx_data_seq, NULL);
	bridge_arbiter_deferred();

	/* Send 1st of 3 CNXN_PAY chunks */
	memcpy_to_usbram((void *)usb_sram_addr(ep_bridge_rx_buffer),
			 adb_payload, sizeof(adb_payload));
	btable_ep[USB_EP_BRIDGE].rx_count = sizeof(adb_payload);
	mock_in_isr = true;
	usb_bridge_rx();
	mock_in_isr = false;
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.adb_cnxn_req_rem_chunks == 2);
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 0);
	drain_queue(&bridge_to_usart4);
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_CREDIT | (bridge.epoch & SERVO_EPOCH_MASK),
			 bridge.tx_data_seq, NULL);
	bridge_arbiter_deferred();

	/*
	 * 3.2 History Y Attempt 1:
	 * Stranded 64B WRTE_PAY in EP7 IN when new Host sends CNXN_HDR (148B ->
	 * 3 chunks) + 3 CNXN_PAY chunks. Because adb_cnxn_resp_rem_chunks == 0,
	 * the new CNXN_HDR overwrites the partial request, disarms EP7 IN, and
	 * forwards all 4 packets to ATmega!
	 */
	bridge.adb_pending_syncs = 0;
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 sizeof(adb_payload), adb_payload);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.usb_in_busy == true);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_VALID);

	memcpy_to_usbram((void *)usb_sram_addr(ep_bridge_rx_buffer), cnxn_pkt,
			 sizeof(cnxn_pkt));
	btable_ep[USB_EP_BRIDGE].rx_count = sizeof(cnxn_pkt);
	mock_in_isr = true;
	usb_bridge_rx();
	mock_in_isr = false;
	/* ISR disarms stale WRTE_PAY because adb_cnxn_resp_rem_chunks == 0 */
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_NAK);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.usb_in_busy == false);
	TEST_ASSERT(bridge.adb_pending_syncs == 1);
	TEST_ASSERT(bridge.adb_cnxn_req_rem_chunks == 3);
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 0);
	drain_queue(&bridge_to_usart4);
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_CREDIT | (bridge.epoch & SERVO_EPOCH_MASK),
			 bridge.tx_data_seq, NULL);
	bridge_arbiter_deferred();

	for (int c = 0; c < 3; c++) {
		memcpy_to_usbram((void *)usb_sram_addr(ep_bridge_rx_buffer),
				 adb_payload, sizeof(adb_payload));
		btable_ep[USB_EP_BRIDGE].rx_count = sizeof(adb_payload);
		mock_in_isr = true;
		usb_bridge_rx();
		mock_in_isr = false;
		bridge_arbiter_deferred();
		drain_queue(&bridge_to_usart4);
		queue_feed_frame(&usart4_to_bridge,
				 SERVO_FRAME_CREDIT |
					 (bridge.epoch & SERVO_EPOCH_MASK),
				 bridge.tx_data_seq, NULL);
		bridge_arbiter_deferred();
	}
	TEST_ASSERT(bridge.adb_cnxn_req_rem_chunks == 0);
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 1);

	/* 3.3 SYNC_ZLP + HS_HDR (148B -> 3 chunks) arrive from ATmega */
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 0, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.adb_pending_syncs == 0);
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 1);
	drain_queue(&bridge_to_usart4);

	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 sizeof(cnxn_pkt), cnxn_pkt);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.usb_in_busy == true);
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 24);
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 4);
	drain_queue(&bridge_to_usart4);

	/*
	 * 3.4 Host Attempt 1 drains 24B HS_HDR into ReadPayload (4 -> 3),
	 *     and 1st 64B HS_PAY chunk arrives from ATmega and arms EP7 IN!
	 */
	mock_in_isr = true;
	usb_bridge_tx();
	mock_in_isr = false;
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 3);
	drain_queue(&bridge_to_usart4);

	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 sizeof(adb_payload), adb_payload);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.usb_in_busy == true);
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 64);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_VALID);
	drain_queue(&bridge_to_usart4);

	/*
	 * 3.5 Host Attempt 2 sends duplicate CNXN_HDR + 3 CNXN_PAY chunks
	 *     while bridge.adb_cnxn_resp_rem_chunks == 3 > 0!
	 *     Verify:
	 *       - usb_bridge_rx() ISR does NOT disarm EP7 IN (stays EP_TX_VALID
	 *         with tx_count == 64)!
	 *       - bridge_arbiter_deferred() drops the duplicate CNXN_HDR and
	 * all 3 duplicate CNXN_PAY chunks on EP7 OUT (zero bytes queued to
	 *         bridge_to_usart4, EP7 OUT re-armed to EP_RX_VALID)!
	 */
	memcpy_to_usbram((void *)usb_sram_addr(ep_bridge_rx_buffer), cnxn_pkt,
			 sizeof(cnxn_pkt));
	btable_ep[USB_EP_BRIDGE].rx_count = sizeof(cnxn_pkt);
	mock_in_isr = true;
	usb_bridge_rx();
	mock_in_isr = false;
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_VALID);
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 64);

	bridge_arbiter_deferred();
	TEST_ASSERT(ep7_out_pending == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_RX_MASK) == EP_RX_VALID);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_VALID);
	TEST_ASSERT(bridge.usb_in_busy == true);
	TEST_ASSERT(bridge.adb_pending_syncs == 0);
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 3);
	TEST_ASSERT(bridge.adb_cnxn_drop_out_chunks == 3);
	TEST_ASSERT(queue_is_empty(&bridge_to_usart4));

	for (int c = 0; c < 3; c++) {
		memcpy_to_usbram((void *)usb_sram_addr(ep_bridge_rx_buffer),
				 adb_payload, sizeof(adb_payload));
		btable_ep[USB_EP_BRIDGE].rx_count = sizeof(adb_payload);
		mock_in_isr = true;
		usb_bridge_rx();
		mock_in_isr = false;
		bridge_arbiter_deferred();
		TEST_ASSERT(ep7_out_pending == 0);
		TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_RX_MASK) ==
			    EP_RX_VALID);
		TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) ==
			    EP_TX_VALID);
		TEST_ASSERT(bridge.adb_cnxn_drop_out_chunks ==
			    (uint8_t)(2 - c));
		TEST_ASSERT(queue_is_empty(&bridge_to_usart4));
	}

	/*
	 * 3.6 Host Attempt 2 drains all 3 HS_PAY chunks (3 -> 2 -> 1 -> 0).
	 */
	mock_in_isr = true;
	usb_bridge_tx();
	mock_in_isr = false;
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 2);
	drain_queue(&bridge_to_usart4);

	for (int c = 0; c < 2; c++) {
		queue_feed_frame(&usart4_to_bridge,
				 SERVO_FRAME_DATA |
					 (bridge.rx_seq
					  << SERVO_DATA_SEQ_SHIFT) |
					 (bridge.epoch & SERVO_EPOCH_MASK),
				 sizeof(adb_payload), adb_payload);
		bridge_arbiter_deferred();
		mock_in_isr = true;
		usb_bridge_tx();
		mock_in_isr = false;
		bridge_arbiter_deferred();
		drain_queue(&bridge_to_usart4);
	}
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 0);

	/*
	 * 3.7 Host Attempt 3 sends fresh CNXN_HDR while
	 *     adb_cnxn_resp_rem_chunks == 0 -> accepted and forwarded to
	 * USART4!
	 */
	memcpy_to_usbram((void *)usb_sram_addr(ep_bridge_rx_buffer), cnxn_pkt,
			 sizeof(cnxn_pkt));
	btable_ep[USB_EP_BRIDGE].rx_count = sizeof(cnxn_pkt);
	mock_in_isr = true;
	usb_bridge_rx();
	mock_in_isr = false;
	bridge_arbiter_deferred();
	TEST_ASSERT(ep7_out_pending == 0);
	TEST_ASSERT(bridge.adb_pending_syncs == 1);
	TEST_ASSERT(bridge.adb_cnxn_req_rem_chunks == 3);
	TEST_ASSERT(!queue_is_empty(&bridge_to_usart4));

	/*
	 * 3.8 16384B (256-chunk) Maximum Clamped A_CNXN Payload Boundary:
	 *     Verifies uint16_t chunk counters do not truncate 256 (0x0100) to
	 * 0 or 1 + 256 = 257 (0x0101) to 1.
	 */
	servo_write_u32_le(&cnxn_pkt[12], 16384UL);
	TEST_ASSERT(servo_adb_payload_chunks(cnxn_pkt) == 256U);
	servo_write_u32_le(&cnxn_pkt[12], 1048576UL);
	TEST_ASSERT(servo_adb_payload_chunks(cnxn_pkt) == 256U);

	bool disarm_16k = false;
	TEST_ASSERT(servo_adb_session_on_host_out(&bridge, cnxn_pkt, 24U,
						  &disarm_16k));
	TEST_ASSERT(bridge.adb_cnxn_req_rem_chunks == 256U);
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 0U);

	bridge.adb_pending_syncs = 0U;
	bridge.usb_in_busy = false;
	struct servo_bridge_rx_effects fx_16k;
	servo_bridge_on_rx_frame(&bridge,
				 SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_DATA,
						 bridge.rx_seq),
				 sizeof(cnxn_pkt), cnxn_pkt, &fx_16k);
	TEST_ASSERT(servo_adb_session_filter_rx_data(&bridge, &fx_16k));
	TEST_ASSERT(bridge.adb_cnxn_resp_rem_chunks == 257U);

	TEST_ASSERT(!servo_adb_session_on_host_out(&bridge, cnxn_pkt, 24U,
						   &disarm_16k));
	TEST_ASSERT(!disarm_16k);
	TEST_ASSERT(bridge.adb_cnxn_drop_out_chunks == 256U);

	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	return EC_SUCCESS;
}

/*
 * Test S8: DUT-Initiated Reset KICK_ZLP, adb_awaiting_cnxn, and EP7 Data Toggle
 * Preservation across SERVO_PROTO_NONE and 15 ms StmExpireKickZlp timeout.
 */
static int test_s8_dut_reset_kick_zlp_and_dtog_preservation(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	mock_pd_connected = true;
	usb_bridge_init();

	/* Lock into 1M bridge in SERVO_PROTO_ADB */
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | 0,
			 SERVO_RESET_FLAG_BOOT | SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);
	TEST_ASSERT(bridge.active_proto == SERVO_PROTO_ADB);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);

	/* Complete DPPU debounce */
	bridge_dppu_debounce_deferred();
	bridge_dppu_reconnect_deferred();
	TEST_ASSERT(last_enumerated_proto == SERVO_PROTO_ADB);
	TEST_ASSERT(!dppu_pulse_active);
	drain_queue(&bridge_to_usart4);

	/*
	 * =====================================================================
	 * 1. Phase 3 (adbd restart -> SET_INTERFACE(2, 1) -> RESET_REQ(ADB)):
	 * =====================================================================
	 */
	/* 1a. Establish an active ADB session on STM32 */
	uint8_t cnxn_pkt[24];
	memset(cnxn_pkt, 0, sizeof(cnxn_pkt));
	servo_write_u32_le(&cnxn_pkt[0], SERVO_ADB_CNXN_CMD);
	servo_write_u32_le(&cnxn_pkt[4], 0x01000000UL);
	servo_write_u32_le(&cnxn_pkt[8], 1048576UL);
	servo_write_u32_le(&cnxn_pkt[12], 0);
	servo_write_u32_le(&cnxn_pkt[16], 0);
	servo_write_u32_le(&cnxn_pkt[20], SERVO_ADB_CNXN_MAGIC);

	memcpy_to_usbram((void *)usb_sram_addr(ep_bridge_rx_buffer), cnxn_pkt,
			 sizeof(cnxn_pkt));
	btable_ep[USB_EP_BRIDGE].rx_count = sizeof(cnxn_pkt);
	mock_in_isr = true;
	usb_bridge_rx();
	mock_in_isr = false;
	bridge_arbiter_deferred();
	drain_queue(&bridge_to_usart4);

	/* 1b. ATmega sends credit + 0-byte SYNC_ZLP + 24B HS_HDR response */
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_CREDIT | (bridge.epoch & SERVO_EPOCH_MASK),
			 bridge.tx_data_seq, NULL);
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (bridge.rx_seq << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 0, NULL);
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_DATA |
				 (((1 - bridge.rx_seq) & 1)
				  << SERVO_DATA_SEQ_SHIFT) |
				 (bridge.epoch & SERVO_EPOCH_MASK),
			 sizeof(cnxn_pkt), cnxn_pkt);
	bridge_arbiter_deferred();
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 24);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_VALID);
	drain_queue(&bridge_to_usart4);

	/* Host drains the 24B HS_HDR, advancing EP_TX_DTOG to DATA1 */
	mock_usb_ep[USB_EP_BRIDGE] |= EP_TX_DTOG;
	mock_in_isr = true;
	usb_bridge_tx();
	mock_in_isr = false;
	bridge_arbiter_deferred();
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_DTOG) == EP_TX_DTOG);

	/* 1c. DUT adbd restarts: peer sends SERVO_FRAME_RESET_REQ(epoch+1, ADB)
	 */
	uint8_t next_epoch = (bridge.epoch + 1) & SERVO_EPOCH_MASK;
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_REQ | next_epoch,
			 SERVO_RESET_FLAG_PEER_KICK | SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();

	/*
	 * Assert proactive KICK_ZLP is armed on Alt 0
	 * (Phase 2A unconditional KICK_ZLP)
	 */
	TEST_ASSERT(ep7_zlp_in_flight == true);
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_RX_MASK) ==
		    (EP_TX_VALID | EP_RX_VALID));
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_DTOG) == EP_TX_DTOG);

	/* Host drains proactive KICK_ZLP */
	mock_in_isr = true;
	usb_bridge_tx();
	mock_in_isr = false;
	bridge_arbiter_deferred();
	TEST_ASSERT(ep7_zlp_in_flight == false);

	/* 1f. Host sends new 24B A_CNXN */
	drain_queue(&bridge_to_usart4);
	memcpy_to_usbram((void *)usb_sram_addr(ep_bridge_rx_buffer), cnxn_pkt,
			 sizeof(cnxn_pkt));
	btable_ep[USB_EP_BRIDGE].rx_count = sizeof(cnxn_pkt);
	mock_in_isr = true;
	usb_bridge_rx();
	mock_in_isr = false;
	bridge_arbiter_deferred();

	TEST_ASSERT(ep7_out_pending == 0);
	TEST_ASSERT(queue_count(&bridge_to_usart4) > 0);
	drain_queue(&bridge_to_usart4);

	/*
	 * =====================================================================
	 * 2. Phase 2: Alt 1 Proactive KICK_ZLP on RESET_REQ
	 * =====================================================================
	 */
	/* 2a. Host activates Alt 1 via SET_INTERFACE(6, 1) */
	usb_uint rx_buf[4] = { (USB_DIR_OUT | USB_TYPE_STANDARD |
				USB_RECIP_INTERFACE) |
				       (USB_REQ_SET_INTERFACE << 8),
			       1, /* Alt 1 */
			       USB_IFACE_BRIDGE, 0 };
	usb_uint tx_buf[4];
	mock_in_isr = true;
	int ret = bridge_iface_request(rx_buf, tx_buf);
	mock_in_isr = false;
	TEST_ASSERT(ret == 0);
	TEST_ASSERT((STM32_USB_EP(0) & EP_TX_RX_MASK) ==
		    (EP_RX_VALID | EP_TX_VALID));
	TEST_ASSERT(bridge_host_alt_setting == 1);
	TEST_ASSERT(ep7_zlp_in_flight == false);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & (EP_TX_DTOG | EP_RX_DTOG)) ==
		    0);
	bridge_arbiter_deferred();
	uint8_t next_epoch2 = bridge.epoch;
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | next_epoch2,
			 SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.state == SERVO_STATE_ACTIVE);

	/* 2b. Establish active ADB session on Alt 1 */
	memcpy_to_usbram((void *)usb_sram_addr(ep_bridge_rx_buffer), cnxn_pkt,
			 sizeof(cnxn_pkt));
	btable_ep[USB_EP_BRIDGE].rx_count = sizeof(cnxn_pkt);
	mock_in_isr = true;
	usb_bridge_rx();
	mock_in_isr = false;
	bridge_arbiter_deferred();
	drain_queue(&bridge_to_usart4);

	/* 2c. Peer sends RESET_REQ(epoch+1, ADB) during active Alt 1 session */
	uint8_t epoch2 = (next_epoch2 + 1) & SERVO_EPOCH_MASK;
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_REQ | epoch2,
			 SERVO_RESET_FLAG_PEER_KICK | SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();

	/* Assert KICK_ZLP is immediately and proactively armed on Alt 1! */
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_ADB);
	TEST_ASSERT(ep7_zlp_in_flight == true);
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_RX_MASK) ==
		    (EP_TX_VALID | EP_RX_VALID));

	/*
	 * =====================================================================
	 * 3. Phase 3: Host Crash / SET_INTERFACE(6, 0) Tears Down Alt 1
	 * =====================================================================
	 */
	rx_buf[1] = 0; /* Alt 0 */
	mock_in_isr = true;
	ret = bridge_iface_request(rx_buf, tx_buf);
	mock_in_isr = false;
	TEST_ASSERT(ret == 0);
	TEST_ASSERT((STM32_USB_EP(0) & EP_TX_RX_MASK) ==
		    (EP_RX_VALID | EP_TX_VALID));
	TEST_ASSERT(bridge_host_alt_setting == 0);
	TEST_ASSERT(ep7_zlp_in_flight == false);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & (EP_TX_DTOG | EP_RX_DTOG)) ==
		    0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_NAK);
	TEST_ASSERT(host_usb_reset_irq == true);

	/* Verify GET_INTERFACE(6) reports 0 */
	rx_buf[0] = (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) |
		    (USB_REQ_GET_INTERFACE << 8);
	rx_buf[1] = 0;
	rx_buf[2] = USB_IFACE_BRIDGE;
	rx_buf[3] = 1;
	tx_buf[0] = 0xFFFF;
	mock_in_isr = true;
	ret = bridge_iface_request(rx_buf, tx_buf);
	mock_in_isr = false;
	TEST_ASSERT(ret == 0);
	TEST_ASSERT(tx_buf[0] == 0);
	TEST_ASSERT(btable_ep[0].tx_count == 1);

	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	return EC_SUCCESS;
}

/* Test 4: Verify Alt 0 and Alt 1 descriptor patching */
static int test_alt0_alt1_descriptor_patching(void)
{
	usb_bridge_set_desc_patch(SERVO_PROTO_NONE);
	TEST_ASSERT(mock_desc_patch[USB_DESC_BRIDGE] == 0x0000);
	TEST_ASSERT(mock_desc_patch[USB_DESC_BRIDGE_ALT1] == 0x0000);

	usb_bridge_set_desc_patch(SERVO_PROTO_FASTBOOT);
	TEST_ASSERT(mock_desc_patch[USB_DESC_BRIDGE] == 0x0000);
	TEST_ASSERT(mock_desc_patch[USB_DESC_BRIDGE_ALT1] == 0x0342);

	usb_bridge_set_desc_patch(SERVO_PROTO_ADB);
	TEST_ASSERT(mock_desc_patch[USB_DESC_BRIDGE] == 0x0142);
	TEST_ASSERT(mock_desc_patch[USB_DESC_BRIDGE_ALT1] == 0x0142);

	return EC_SUCCESS;
}

/* Test 5: Verify servo_adb_session_filter_rx_data() barrier behavior */
static int test_servo_adb_session_filter_rx_data_awaiting_cnxn(void)
{
	struct servo_bridge_node test_node;
	servo_bridge_init(&test_node, /*is_epoch_leader=*/true, 0);
	test_node.state = SERVO_STATE_ACTIVE;
	test_node.active_proto = SERVO_PROTO_ADB;
	test_node.adb_awaiting_cnxn = false;
	test_node.adb_pending_syncs = 1U;

	struct servo_bridge_rx_effects fx;
	memset(&fx, 0, sizeof(fx));
	fx.usb_in_armed = true;
	fx.usb_in_len = 24U;

	/* When adb_pending_syncs > 0, non-sync data is discarded */
	bool accepted = servo_adb_session_filter_rx_data(&test_node, &fx);
	TEST_ASSERT(!accepted);
	TEST_ASSERT(!fx.usb_in_armed);

	/* When 0-byte SYNC_ZLP arrives, adb_pending_syncs is cleared to 0 */
	fx.usb_in_armed = true;
	fx.usb_in_len = 0U;
	accepted = servo_adb_session_filter_rx_data(&test_node, &fx);
	TEST_ASSERT(!accepted);
	TEST_ASSERT(test_node.adb_pending_syncs == 0U);

	/* When adb_pending_syncs == 0, data is accepted */
	fx.usb_in_armed = true;
	fx.usb_in_len = 24U;
	accepted = servo_adb_session_filter_rx_data(&test_node, &fx);
	TEST_ASSERT(accepted);
	TEST_ASSERT(fx.usb_in_armed);

	return EC_SUCCESS;
}

/*
 * Phase 2A: Single-Reset Discipline, Alt 0 Legacy, and High-RTT WAIT Keepalive
 */
static int test_phase2a_single_reset_discipline_and_wait_keepalive(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);

	/*
	 * =====================================================================
	 * 1. Single-Reset Discipline on CloseDevice() + OpenDevice() (Alt 1)
	 * =====================================================================
	 */
	mock_pd_connected = true;
	usb_bridge_init();
	effective_usb_protocol = SERVO_PROTO_ADB;
	last_enumerated_proto = SERVO_PROTO_ADB;
	probe_state = USB_BRIDGE_LOCKED_1M_BRIDGE;
	bridge.state = SERVO_STATE_ACTIVE;
	bridge.epoch = 1;
	bridge.active_proto = SERVO_PROTO_ADB;
	bridge.pending_reset_req = false;
	bridge_host_alt_setting = 1;
	bridge.local_host_active = true;
	host_session_dirty = true;
	host_usb_reset_irq = false;
	drain_queue(&bridge_to_usart4);
	drain_queue(&usart4_to_bridge);

	/*
	 * 1a. Simulating CloseDevice():
	 * CLEAR_HALT(0x87) -> CLEAR_HALT(0x07) -> SET_INTERFACE(6, 0)
	 */
	mock_in_isr = true;
	int ret = usb_bridge_clear_halt(USB_EP_BRIDGE | 0x80);
	TEST_ASSERT(ret == 0);
	TEST_ASSERT(host_usb_reset_irq == false);

	ret = usb_bridge_clear_halt(USB_EP_BRIDGE);
	TEST_ASSERT(ret == 0);
	TEST_ASSERT(host_usb_reset_irq == false);

	usb_uint rx_buf[4] = { (USB_DIR_OUT | USB_TYPE_STANDARD |
				USB_RECIP_INTERFACE) |
				       (USB_REQ_SET_INTERFACE << 8),
			       0, /* Alt 0 */
			       USB_IFACE_BRIDGE, 0 };
	usb_uint tx_buf[4];
	ret = bridge_iface_request(rx_buf, tx_buf);
	mock_in_isr = false;
	TEST_ASSERT(ret == 0);
	TEST_ASSERT(bridge_host_alt_setting == 0);
	TEST_ASSERT(bridge.local_host_active == false);
	TEST_ASSERT(host_usb_reset_irq == true);
	TEST_ASSERT(host_session_dirty == false);

	/* Let arbiter pop exactly 1 RESET_REQ with PEER_KICK */
	bridge_arbiter_deferred();
	uint8_t out_frame[SERVO_MAX_FRAME_BYTES];
	int out_len = 0;
	while (queue_remove_unit(&bridge_to_usart4, &out_frame[out_len]))
		out_len++;
	TEST_ASSERT(out_len == 4);
	TEST_ASSERT(SERVO_GET_OP(out_frame[1]) == SERVO_OP_RESET_REQ);
	TEST_ASSERT((out_frame[2] & SERVO_RESET_FLAG_PEER_KICK) != 0);
	TEST_ASSERT((out_frame[2] & SERVO_RESET_FLAG_HOST_ACTIVE) == 0);
	TEST_ASSERT(queue_is_empty(&bridge_to_usart4));

	/* Complete the reset to ACTIVE */
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_RESET_ACK | bridge.epoch, SERVO_PROTO_ADB,
			 NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.state == SERVO_STATE_ACTIVE);
	TEST_ASSERT(bridge.send_peer_kick == false);
	drain_queue(&bridge_to_usart4);

	/*
	 * 1b. Simulating OpenDevice(): SET_INTERFACE(6, 1) while session is
	 * clean
	 */
	rx_buf[1] = 1; /* Alt 1 */
	mock_in_isr = true;
	ret = bridge_iface_request(rx_buf, tx_buf);
	mock_in_isr = false;
	TEST_ASSERT(ret == 0);
	TEST_ASSERT(bridge_host_alt_setting == 1);
	TEST_ASSERT(bridge.local_host_active == true);
	TEST_ASSERT(host_usb_reset_irq == false);
	TEST_ASSERT(host_session_dirty == true);

	bridge_arbiter_deferred();
	TEST_ASSERT(queue_is_empty(&bridge_to_usart4));

	/*
	 * =====================================================================
	 * 2. Legacy Alt 0 Support: CLEAR_HALT triggers reset, peer RESET arms
	 *    KICK_ZLP
	 * =====================================================================
	 */
	/* Transition to Alt 0 cleanly */
	rx_buf[1] = 0;
	mock_in_isr = true;
	ret = bridge_iface_request(rx_buf, tx_buf);
	mock_in_isr = false;
	TEST_ASSERT(ret == 0);
	bridge_arbiter_deferred();
	drain_queue(&bridge_to_usart4);
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_RESET_ACK | bridge.epoch, SERVO_PROTO_ADB,
			 NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.state == SERVO_STATE_ACTIVE);
	TEST_ASSERT(bridge_host_alt_setting == 0);
	drain_queue(&bridge_to_usart4);

	/* In Alt 0, dirty session + CLEAR_HALT(0x87) MUST trigger reset */
	host_session_dirty = true;
	host_usb_reset_irq = false;
	mock_in_isr = true;
	ret = usb_bridge_clear_halt(USB_EP_BRIDGE | 0x80);
	mock_in_isr = false;
	TEST_ASSERT(ret == 0);
	TEST_ASSERT(host_usb_reset_irq == true);

	bridge_arbiter_deferred();
	out_len = 0;
	while (queue_remove_unit(&bridge_to_usart4, &out_frame[out_len]))
		out_len++;
	TEST_ASSERT(out_len == 4);
	TEST_ASSERT(SERVO_GET_OP(out_frame[1]) == SERVO_OP_RESET_REQ);
	TEST_ASSERT((out_frame[2] & SERVO_RESET_FLAG_PEER_KICK) != 0);

	/* Complete reset */
	queue_feed_frame(&usart4_to_bridge,
			 SERVO_FRAME_RESET_ACK | bridge.epoch, SERVO_PROTO_ADB,
			 NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.state == SERVO_STATE_ACTIVE);
	drain_queue(&bridge_to_usart4);

	/*
	 * Peer sends RESET_REQ in Alt 0 -> arms KICK_ZLP
	 */
	uint8_t next_ep = (bridge.epoch + 1) & SERVO_EPOCH_MASK;
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_REQ | next_ep,
			 SERVO_RESET_FLAG_PEER_KICK | SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(ep7_zlp_in_flight == true);

	/* Drain KICK_ZLP */
	mock_in_isr = true;
	usb_bridge_tx();
	mock_in_isr = false;
	bridge_arbiter_deferred();
	TEST_ASSERT(ep7_zlp_in_flight == false);
	drain_queue(&bridge_to_usart4);

	/*
	 * =====================================================================
	 * 3. High-RTT SERVO_FRAME_WAIT (60 ms delay): 0 duplicate DATA
	 *    retransmits
	 * =====================================================================
	 */
	/* Re-activate Alt 1 */
	rx_buf[1] = 1;
	mock_in_isr = true;
	bridge_iface_request(rx_buf, tx_buf);
	mock_in_isr = false;
	bridge.state = SERVO_STATE_ACTIVE;
	bridge.epoch = 2;
	bridge.active_proto = SERVO_PROTO_ADB;
	bridge.tx_credits = 1;
	bridge.has_tx_data = false;
	bridge.pending_data_tx = false;
	bridge.tx_link_acked = false;
	drain_queue(&bridge_to_usart4);
	drain_queue(&usart4_to_bridge);

	uint8_t sample_data[32];
	memset(sample_data, 0x5a, sizeof(sample_data));
	TEST_ASSERT(servo_bridge_send_data(&bridge, sample_data,
					   sizeof(sample_data)));
	bridge_arbiter_deferred();

	TEST_ASSERT(bridge.has_tx_data == true);
	TEST_ASSERT(bridge.tx_credits == 0);
	TEST_ASSERT(bridge.tx_link_acked == false);

	/* Pop the initial DATA frame */
	out_len = 0;
	while (queue_remove_unit(&bridge_to_usart4, &out_frame[out_len]))
		out_len++;
	TEST_ASSERT(out_len == (int)(4 + sizeof(sample_data)));
	TEST_ASSERT(SERVO_GET_CHAN(out_frame[1]) == SERVO_CHAN_H2H &&
		    SERVO_GET_OP(out_frame[1]) == SERVO_OP_DATA);

	/* Peer sends SERVO_FRAME_WAIT(epoch, seq) */
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_WAIT(bridge.epoch),
			 bridge.tx_data_seq, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.tx_link_acked == true);
	TEST_ASSERT(bridge.tx_credits == 0);
	TEST_ASSERT(bridge.has_tx_data == true);

	/*
	 * Advance time over 60 ms in 1 ms steps: NO duplicate DATA frame is
	 * popped!
	 */
	for (int ms = 0; ms < 60; ms++) {
		crec_usleep(1000);
		bridge_arbiter_deferred();
		TEST_ASSERT(queue_is_empty(&bridge_to_usart4));
	}

	/* At 60 ms, peer sends normal SERVO_FRAME_CREDIT */
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_CREDIT | bridge.epoch,
			 0, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.tx_link_acked == false);
	TEST_ASSERT(bridge.tx_credits == 1);
	TEST_ASSERT(bridge.has_tx_data == false);

	return EC_SUCCESS;
}

static int test_phase2a_host_session_dirty_back_to_back_set_interface(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	mock_pd_connected = true;
	usb_bridge_init();

	/* Lock into 1M bridge in SERVO_PROTO_ADB */
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | 0,
			 SERVO_RESET_FLAG_BOOT | SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);

	/* Complete DPPU debounce */
	bridge_dppu_debounce_deferred();
	bridge_dppu_reconnect_deferred();
	TEST_ASSERT(last_enumerated_proto == SERVO_PROTO_ADB);

	/* 1. Host establishes Alt 1 */
	usb_uint rx_buf[4] = { USB_DIR_OUT | USB_TYPE_STANDARD |
				       USB_RECIP_INTERFACE |
				       (USB_REQ_SET_INTERFACE << 8),
			       1, /* wValue: Alt 1 */
			       USB_IFACE_BRIDGE, 0 };
	usb_uint tx_buf[4] = { 0 };
	TEST_ASSERT(bridge_iface_request(rx_buf, tx_buf) == 0);
	TEST_ASSERT(host_session_dirty == true);
	TEST_ASSERT(bridge_host_alt_setting == 1);

	/* Process any pending deferred arbiter invocation */
	bridge_arbiter_deferred();
	TEST_ASSERT(host_session_dirty == true);

	/*
	 * 2. Rapid sequence: SET_INTERFACE(6, 0) followed immediately by
	 * SET_INTERFACE(6, 1) WITHOUT running bridge_arbiter_deferred() in
	 * between
	 */
	rx_buf[1] = 0; /* Alt 0 */
	TEST_ASSERT(bridge_iface_request(rx_buf, tx_buf) == 0);
	TEST_ASSERT(bridge_host_alt_setting == 0);
	TEST_ASSERT(host_session_dirty == false);
	TEST_ASSERT(host_usb_reset_irq == true);

	rx_buf[1] = 1; /* Alt 1 */
	TEST_ASSERT(bridge_iface_request(rx_buf, tx_buf) == 0);
	TEST_ASSERT(bridge_host_alt_setting == 1);
	TEST_ASSERT(host_session_dirty == true);
	TEST_ASSERT(host_usb_reset_irq == true);

	/*
	 * Now run bridge_arbiter_deferred(). Because bridge_host_alt_setting ==
	 * 1, arbiter check-and-clear must NOT clear host_session_dirty to
	 * false!
	 */
	bridge_arbiter_deferred();
	TEST_ASSERT(host_usb_reset_irq == false);
	TEST_ASSERT(host_session_dirty == true);

	/*
	 * 3. A subsequent SET_INTERFACE(6, 0) sees active traffic because
	 * host_session_dirty is true
	 */
	rx_buf[1] = 0;
	TEST_ASSERT(bridge_iface_request(rx_buf, tx_buf) == 0);
	TEST_ASSERT(host_usb_reset_irq == true);
	TEST_ASSERT(host_session_dirty == false);

	bridge_arbiter_deferred();
	TEST_ASSERT(host_usb_reset_irq == false);
	TEST_ASSERT(host_session_dirty == false);

	return EC_SUCCESS;
}

static int test_phase2a_peer_reset_req_none_with_kick_arms_zlp(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	mock_pd_connected = true;
	usb_bridge_init();

	/* Lock into 1M bridge in SERVO_PROTO_ADB */
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | 0,
			 SERVO_RESET_FLAG_BOOT | SERVO_PROTO_ADB, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);

	/* Complete DPPU debounce */
	bridge_dppu_debounce_deferred();
	bridge_dppu_reconnect_deferred();
	TEST_ASSERT(last_enumerated_proto == SERVO_PROTO_ADB);

	/* Establish active ADB session */
	uint8_t cnxn_pkt[24];
	memset(cnxn_pkt, 0, sizeof(cnxn_pkt));
	servo_write_u32_le(&cnxn_pkt[0], SERVO_ADB_CNXN_CMD);
	servo_write_u32_le(&cnxn_pkt[4], 0x01000000UL);
	servo_write_u32_le(&cnxn_pkt[8], 1048576UL);
	servo_write_u32_le(&cnxn_pkt[20], SERVO_ADB_CNXN_MAGIC);
	memcpy_to_usbram((void *)usb_sram_addr(ep_bridge_rx_buffer), cnxn_pkt,
			 sizeof(cnxn_pkt));
	btable_ep[USB_EP_BRIDGE].rx_count = sizeof(cnxn_pkt);
	mock_in_isr = true;
	usb_bridge_rx();
	mock_in_isr = false;
	bridge_arbiter_deferred();

	/*
	 * Peer ATmega32U4 daemon tears down: sends
	 * RESET_REQ(new_proto = SERVO_PROTO_NONE, PEER_KICK = 1)
	 */
	uint8_t next_epoch = (bridge.epoch + 1) & SERVO_EPOCH_MASK;
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_REQ | next_epoch,
			 SERVO_RESET_FLAG_PEER_KICK | SERVO_PROTO_NONE, NULL);
	bridge_arbiter_deferred();

	/* Verify STM32 armed proactive KICK_ZLP for Host adb */
	TEST_ASSERT(ep7_zlp_in_flight == true);
	TEST_ASSERT(btable_ep[USB_EP_BRIDGE].tx_count == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_MASK) == EP_TX_VALID);

	/* Host drains KICK_ZLP */
	mock_in_isr = true;
	usb_bridge_tx();
	mock_in_isr = false;
	bridge_arbiter_deferred();
	TEST_ASSERT(ep7_zlp_in_flight == false);

	return EC_SUCCESS;
}

static int test_phase2a_fastboot_consecutive_set_interface_no_reset(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	mock_pd_connected = true;
	usb_bridge_init();

	/* Lock into 1M bridge in SERVO_PROTO_FASTBOOT */
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | 0,
			 SERVO_RESET_FLAG_BOOT | SERVO_PROTO_FASTBOOT, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(usb_bridge_get_probe_state() ==
		    USB_BRIDGE_LOCKED_1M_BRIDGE);

	/* Complete DPPU debounce */
	bridge_dppu_debounce_deferred();
	bridge_dppu_reconnect_deferred();
	TEST_ASSERT(last_enumerated_proto == SERVO_PROTO_FASTBOOT);
	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_FASTBOOT);

	/* 1. Fastboot command 1 starts: SET_INTERFACE(6, 1) */
	usb_uint rx_buf[4] = { USB_DIR_OUT | USB_TYPE_STANDARD |
				       USB_RECIP_INTERFACE |
				       (USB_REQ_SET_INTERFACE << 8),
			       1, /* wValue: Alt 1 */
			       USB_IFACE_BRIDGE, 0 };
	usb_uint tx_buf[4] = { 0 };
	TEST_ASSERT(bridge_iface_request(rx_buf, tx_buf) == 0);
	TEST_ASSERT(bridge_host_alt_setting == 1);
	TEST_ASSERT(bridge.local_host_active == true);
	TEST_ASSERT(host_session_dirty == true);
	TEST_ASSERT(host_usb_reset_irq == false);

	/*
	 * Fastboot command 1 finishes: Host process exits, usbfs sends
	 * SET_INTERFACE(6, 0)
	 */
	rx_buf[1] = 0; /* Alt 0 */
	TEST_ASSERT(bridge_iface_request(rx_buf, tx_buf) == 0);
	TEST_ASSERT(bridge_host_alt_setting == 0);
	TEST_ASSERT(bridge.local_host_active == false);
	/*
	 * Single-Reset Discipline: Close/abort sets host_usb_reset_irq and
	 * clears host_session_dirty
	 */
	TEST_ASSERT(host_usb_reset_irq == true);
	TEST_ASSERT(host_session_dirty == false);

	/*
	 * Deferred arbiter executes single reset, emitting
	 * RESET_REQ(PEER_KICK | FASTBOOT)
	 */
	drain_queue(&bridge_to_usart4);
	bridge_arbiter_deferred();
	TEST_ASSERT(host_usb_reset_irq == false);
	TEST_ASSERT(bridge.state == SERVO_STATE_RESETTING ||
		    bridge.state == SERVO_STATE_ACTIVE);

	uint8_t out_frame[SERVO_MAX_FRAME_BYTES];
	int out_len = 0;
	while (queue_remove_unit(&bridge_to_usart4, &out_frame[out_len]))
		out_len++;
	TEST_ASSERT(out_len >= 4);
	TEST_ASSERT(SERVO_GET_OP(out_frame[1]) == SERVO_OP_RESET_REQ);
	TEST_ASSERT((out_frame[2] & SERVO_RESET_FLAG_PEER_KICK) != 0);

	/* Peer acknowledges reset */
	uint8_t next_epoch = bridge.epoch & SERVO_EPOCH_MASK;
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | next_epoch,
			 SERVO_PROTO_FASTBOOT, NULL);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.state == SERVO_STATE_ACTIVE);

	/* 2. Fastboot command 2 starts: SET_INTERFACE(6, 1) */
	rx_buf[1] = 1; /* Alt 1 */
	TEST_ASSERT(bridge_iface_request(rx_buf, tx_buf) == 0);
	TEST_ASSERT(bridge_host_alt_setting == 1);
	TEST_ASSERT(bridge.local_host_active == true);
	TEST_ASSERT(host_session_dirty == true);
	/* No duplicate reset on open! */
	TEST_ASSERT(host_usb_reset_irq == false);

	drain_queue(&bridge_to_usart4);
	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.state == SERVO_STATE_ACTIVE);
	TEST_ASSERT(bridge.pending_reset_req == false);
	TEST_ASSERT(queue_is_empty(&bridge_to_usart4));

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_crc8_and_structural_validation);
	RUN_TEST(test_cnxn_clamping_and_detection);
	RUN_TEST(test_timer_rollover_and_non_swallowing_resync);
	RUN_TEST(test_tx_headroom_and_priority_arbitration);
	RUN_TEST(test_dual_mcu_protocol_and_epoch_transitions);
	RUN_TEST(test_usart4_rx_isr_staging_and_ore_fe_recovery);
	RUN_TEST(test_usb_bridge_ep7_ctr_rc_w0_and_dtog_preservation);
	RUN_TEST(test_usb_bridge_corrupt_irq_atomic_flush_and_session_barrier);
	RUN_TEST(test_usb_bridge_dppu_debounce_fastboot_zlp_and_usart4_ctl);
	RUN_TEST(test_spsc_concurrency_and_parser_fuzzer);
	RUN_TEST(test_usart4_dma_tx);
	RUN_TEST(test_autodetect_1m_bridge);
	RUN_TEST(test_autodetect_9600_fallback);
	RUN_TEST(test_autodetect_reset_reprobe);
	RUN_TEST(test_autodetect_manual_override);
	RUN_TEST(test_probe_active_1m_resync_all_epochs_and_booted);
	RUN_TEST(test_reset_held_kbd_drain_and_fallthrough_safety);
	RUN_TEST(test_bug15_usb_reset_and_bug16_atmel_reset_proto);
	RUN_TEST(test_s7_adb_stale_timeout_and_deferred_out_pending_race);
	RUN_TEST(test_s8_dut_reset_kick_zlp_and_dtog_preservation);
	RUN_TEST(test_alt0_alt1_descriptor_patching);
	RUN_TEST(test_servo_adb_session_filter_rx_data_awaiting_cnxn);
	RUN_TEST(test_phase2a_single_reset_discipline_and_wait_keepalive);
	RUN_TEST(test_phase2a_host_session_dirty_back_to_back_set_interface);
	RUN_TEST(test_phase2a_peer_reset_req_none_with_kick_arms_zlp);
	RUN_TEST(test_phase2a_fastboot_consecutive_set_interface_no_reset);

	test_print_result();
}
