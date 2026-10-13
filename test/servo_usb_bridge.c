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

#include "board/servo_v4p1/servo_usb_bridge_frame.h"
#include "board/servo_v4p1/usb_bridge.h"
#include "chip/stm32/registers.h"
#include "chip/stm32/usart.h"
#include "chip/stm32/usb-stream.h"
#include "chip/stm32/usb_hw.h"
#include "common.h"
#include "console.h"
#include "queue.h"
#include "queue_policies.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#include <pthread.h>

#ifndef USB_IFACE_MSC
#define USB_IFACE_MSC 1
#endif
#ifndef USB_EP_MSC
#define USB_EP_MSC 2
#endif
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

/* Hardware register & endpoint mocks */
static uint16_t mock_usb_ep[8];
struct stm32_endpoint btable_ep[8];
static bool mock_in_isr;
static bool mock_irq_depth_violation;
static int mock_ep7_toggle_count;
static int mock_ep2_toggle_count;

static const char *mock_serial_str = "SN12345";

const char *board_read_serial(void)
{
	return mock_serial_str;
}

static void mock_stm32_toggle_ep(int ep, uint16_t mask, uint16_t val,
				 uint16_t flags)
{
	if ((ep == USB_EP_BRIDGE || ep == USB_EP_MSC) && !mock_in_isr) {
		if (irq_disable_depth <= 0)
			mock_irq_depth_violation = true;
		if (ep == USB_EP_BRIDGE)
			mock_ep7_toggle_count++;
		else if (ep == USB_EP_MSC)
			mock_ep2_toggle_count++;
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
static uint32_t mock_usart_cr3;
static uint32_t mock_usart_sr;
static uint32_t mock_usart_icr;

#define MOCK_USART_FIFO_SIZE 256
static uint8_t mock_usart_rx_fifo[MOCK_USART_FIFO_SIZE];
static size_t mock_usart_rx_head;
static size_t mock_usart_rx_tail;

static inline void mock_usart_fifo_reset(void)
{
	mock_usart_rx_head = 0;
	mock_usart_rx_tail = 0;
}

static inline void mock_usart_feed_byte(uint8_t b)
{
	mock_usart_rx_fifo[mock_usart_rx_head] = b;
	mock_usart_rx_head = (mock_usart_rx_head + 1) % MOCK_USART_FIFO_SIZE;
}

static inline uint32_t mock_get_usart_sr(intptr_t base)
{
	mock_usart_sr &= ~(mock_usart_icr & (STM32_USART_ICR_ORECF | BIT(1) |
					     BIT(2) | STM32_USART_ICR_IDLECF));
	mock_usart_icr = 0;
	uint32_t sr = mock_usart_sr;
	if (mock_usart_rx_head != mock_usart_rx_tail)
		sr |= STM32_USART_SR_RXNE;
	else
		sr &= ~STM32_USART_SR_RXNE;
	return sr;
}

static inline uint8_t mock_get_usart_rdr(intptr_t base)
{
	if (mock_usart_rx_head != mock_usart_rx_tail) {
		uint8_t byte = mock_usart_rx_fifo[mock_usart_rx_tail];
		mock_usart_rx_tail =
			(mock_usart_rx_tail + 1) % MOCK_USART_FIFO_SIZE;
		return byte;
	}
	return 0;
}

#undef STM32_USART_CR1
#define STM32_USART_CR1(base) mock_usart_cr1
#undef STM32_USART_CR3
#define STM32_USART_CR3(base) mock_usart_cr3
#undef STM32_USART_SR
#define STM32_USART_SR(base) mock_get_usart_sr(base)
#undef STM32_USART_RDR
#define STM32_USART_RDR(base) mock_get_usart_rdr(base)
#undef STM32_USART_ICR
#define STM32_USART_ICR(base) mock_usart_icr

/* Queues and config */
struct usb_stream_config const usart4_usb = {};
struct queue const usart4_to_bridge = QUEUE_NULL(128, uint8_t);
struct queue const bridge_to_usart4 = QUEUE_NULL(128, uint8_t);
struct queue const usb_to_bridge_kbd = QUEUE_NULL(16, uint8_t);
struct queue const bridge_kbd_to_usb = QUEUE_NULL(16, uint8_t);

static struct usart_hw_config const mock_usart4_hw = {
	.base = STM32_USART4_BASE,
};
static struct usart_state mock_usart4_state;
static struct usart_config const mock_usart4_config = {
	.hw = &mock_usart4_hw,
	.state = &mock_usart4_state,
	.producer = {
		.queue = &usart4_to_bridge,
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
#include "chip/stm32/usart_rx_interrupt-stm32f0.c"

static void drain_queue(struct queue const *q)
{
	uint8_t placeholder;
	while (queue_remove_unit(q, &placeholder))
		;
}

static void queue_feed_frame(struct queue const *q, uint8_t type,
			     uint8_t arg_len, const uint8_t *payload)
{
	uint8_t frame[SERVO_MAX_FRAME_BYTES];
	size_t len = servo_frame_encode(type, arg_len, payload, frame);
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

			if ((type & SERVO_TYPE_RESERVED_BIT) == 0) {
				uint8_t op = type & SERVO_OPCODE_MASK;
				switch (op) {
				case SERVO_FRAME_DATA:
					expected = (arg <= SERVO_MAX_DATA_LEN);
					break;
				case SERVO_FRAME_CREDIT:
				case SERVO_FRAME_NAK:
					expected =
						((type & SERVO_CTRL_SEQ_MASK) ==
						 0) &&
						(arg <= 1);
					break;
				case SERVO_FRAME_RESET_REQ:
				case SERVO_FRAME_RESET_ACK:
					if ((type & SERVO_CTRL_SEQ_MASK) == 0) {
						if ((arg &
						     ~(SERVO_RESET_FLAG_BOOT |
						       SERVO_RESET_PROTO_MASK)) ==
						    0) {
							uint8_t proto =
								arg &
								SERVO_RESET_PROTO_MASK;
							expected =
								servo_is_valid_proto(
									proto);
						}
					}
					break;
				case SERVO_FRAME_KEY_CMD:
				case SERVO_FRAME_KEY_ACK:
					expected = ((type & 0x0F) == 0);
					break;
				case SERVO_FRAME_MSC_REQ:
					expected = ((type & 0x0F) == 0) &&
						   (arg == 8);
					break;
				case SERVO_FRAME_MSC_DATA:
					expected = ((type & 0x0F) == 0) &&
						   (arg >= 1 &&
						    arg <= SERVO_MAX_DATA_LEN);
					break;
				default:
					expected = false;
					break;
				}
			}
			TEST_ASSERT(valid == expected);
		}
	}

	/* 2. Exhaustive single-bit flips across payload lengths 0..64 */
	for (uint8_t len = 0; len <= SERVO_MAX_DATA_LEN; len++) {
		size_t frame_len = servo_frame_encode(SERVO_FRAME_DATA, len,
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
		size_t frame_len = servo_frame_encode(SERVO_FRAME_DATA, len,
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
					if (evt == SERVO_RX_EVT_FRAME_COMPLETE &&
					    i == frame_len - 1)
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
		size_t frame_len = servo_frame_encode(SERVO_FRAME_DATA, len,
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
					if (evt == SERVO_RX_EVT_FRAME_COMPLETE &&
					    i == frame_len - 1)
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

/* 2. Test ADB Connection Clamping & Detection */
static int test_cnxn_clamping_and_detection(void)
{
	uint8_t buffer[64];
	uint8_t backup[64];

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

		memcpy(backup, pkt, 24);
		servo_clamp_adb_cnxn(pkt, 24, SERVO_PROTO_ADB);

		/* arg1 clamped to 16384 */
		TEST_ASSERT(servo_read_u32_le(&pkt[8]) ==
			    SERVO_MAX_PAYLOAD_CLAMP);
		/* Other 20 bytes untouched */
		TEST_ASSERT(memcmp(&pkt[0], &backup[0], 8) == 0);
		TEST_ASSERT(memcmp(&pkt[12], &backup[12], 12) == 0);

		/* arg1 <= 16384 preserved */
		servo_write_u32_le(&pkt[8], 16384UL);
		servo_clamp_adb_cnxn(pkt, 24, SERVO_PROTO_ADB);
		TEST_ASSERT(servo_read_u32_le(&pkt[8]) == 16384UL);

		servo_write_u32_le(&pkt[8], 4096UL);
		servo_clamp_adb_cnxn(pkt, 24, SERVO_PROTO_ADB);
		TEST_ASSERT(servo_read_u32_le(&pkt[8]) == 4096UL);

		/* Non-A_CNXN packet untouched */
		servo_write_u32_le(&pkt[0], 0x4E504F41U); /* "OPEN" */
		servo_write_u32_le(&pkt[8], 1048576UL);
		servo_write_u32_le(&pkt[20], ~0x4E504F41U);
		TEST_ASSERT(!servo_is_adb_cnxn(pkt, 24, SERVO_PROTO_ADB));
		servo_clamp_adb_cnxn(pkt, 24, SERVO_PROTO_ADB);
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
	servo_frame_rx_step(&p, SERVO_FRAME_DATA, 1005);
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

	/* Feed valid frame: TYPE=0x10, LEN=0, CRC */
	servo_frame_rx_step(&p, SERVO_FRAME_DATA, 11010);
	servo_frame_rx_step(&p, 0, 11015);
	uint8_t crc = servo_crc8_ccitt(0, SERVO_FRAME_DATA);
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
	TEST_ASSERT((out[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_RESET_ACK);

	size_t len2 = servo_bridge_pop_tx_frame(&node, 128, out, 0);
	TEST_ASSERT(len2 == 4);
	TEST_ASSERT((out[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_NAK);

	size_t len3 = servo_bridge_pop_tx_frame(&node, 128, out, 0);
	TEST_ASSERT(len3 == 4);
	TEST_ASSERT((out[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_KEY_CMD);

	size_t len4 = servo_bridge_pop_tx_frame(&node, 128, out, 0);
	TEST_ASSERT(len4 == 4 + sizeof(payload));
	TEST_ASSERT((out[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_DATA);

	/* TX Headroom enforcement */
	/* P1 frames require 4 bytes (no headroom) */
	node.pending_credit = true;
	node.pending_credit_epoch = 1;
	node.pending_credit_seq = 0;
	TEST_ASSERT(servo_bridge_pop_tx_frame(&node, 3, out, 0) == 0);
	TEST_ASSERT(servo_bridge_pop_tx_frame(&node, 4, out, 0) == 4);

	/* P2 frames require SERVO_KEY_TX_MIN_FREE (4 + 16 = 20 bytes) */
	node.pending_key_cmd = true;
	node.pending_key_cmd_ch = 'K';
	TEST_ASSERT(servo_bridge_pop_tx_frame(&node, 19, out, 0) == 0);
	TEST_ASSERT(servo_bridge_pop_tx_frame(&node, 20, out, 0) == 4);

	/*
	 * P3 DATA frames require SERVO_DATA_TX_MIN_FREE(len)
	 * (4 + 10 + 16 = 30 bytes).
	 */
	node.has_tx_data = false;
	node.tx_credits = 1;
	servo_bridge_send_data(&node, payload, 10);
	TEST_ASSERT(servo_bridge_pop_tx_frame(&node, 29, out, 0) == 0);
	TEST_ASSERT(servo_bridge_pop_tx_frame(&node, 30, out, 0) == 14);

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

	/* STM32 sends boot RESET_REQ */
	size_t len = servo_bridge_pop_tx_frame(&stm32, 128, out, 0);
	TEST_ASSERT(len == 4);
	TEST_ASSERT((out[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_RESET_REQ);
	servo_bridge_on_rx_frame(&atmega, out[1], out[2], NULL, &fx);
	TEST_ASSERT(atmega.pending_reset_ack);

	/* ATmega sends its boot RESET_REQ (higher priority than
	 * pending_reset_ack) */
	len = servo_bridge_pop_tx_frame(&atmega, 128, out, 0);
	TEST_ASSERT(len == 4);
	TEST_ASSERT((out[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_RESET_REQ);
	servo_bridge_on_rx_frame(&stm32, out[1], out[2], NULL, &fx);
	TEST_ASSERT(stm32.pending_reset_ack);

	/* Now ATmega responds with boot RESET_ACK to STM32's RESET_REQ */
	len = servo_bridge_pop_tx_frame(&atmega, 128, out, 0);
	TEST_ASSERT(len == 4);
	TEST_ASSERT((out[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_RESET_ACK);
	servo_bridge_on_rx_frame(&stm32, out[1], out[2], NULL, &fx);
	TEST_ASSERT(stm32.state == SERVO_STATE_ACTIVE && !stm32.booted);

	/* Now STM32 responds with boot RESET_ACK to ATmega's RESET_REQ */
	len = servo_bridge_pop_tx_frame(&stm32, 128, out, 0);
	TEST_ASSERT(len == 4);
	TEST_ASSERT((out[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_RESET_ACK);
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
	uint8_t type_dup = SERVO_FRAME_DATA | (0 << SERVO_DATA_SEQ_SHIFT) |
			   (stm32.epoch & SERVO_EPOCH_MASK);
	servo_bridge_on_rx_frame(&stm32, type_dup, sizeof(payload), payload,
				 &fx);
	TEST_ASSERT(!fx.usb_in_armed); /* Suppressed! */
	TEST_ASSERT(stm32.pending_credit); /* Credit re-acked! */
	TEST_ASSERT(stm32.pending_credit_seq == 0);

	return EC_SUCCESS;
}

/* 6. Test USART4 RX ISR Staging & ORE/FE Recovery */
static int test_usart4_rx_isr_staging_and_ore_fe_recovery(void)
{
	drain_queue(&usart4_to_bridge);
	mock_usart_fifo_reset();
	mock_usart_cr1 = 0;
	mock_usart_cr3 = STM32_USART_CR3_OVRDIS;
	mock_usart_sr = 0;
	mock_usart_icr = 0;
	mock_usart4_state.rx_overrun = 0;
	mock_usart4_state.rx_dropped = 0;
	usart4_rx_corrupt_irq = false;

	/* 1. Call usart_rx_init on STM32_USART4_BASE */
	usart_rx_init(&mock_usart4_config);
	TEST_ASSERT((mock_usart_cr3 & STM32_USART_CR3_OVRDIS) == 0);
	TEST_ASSERT((mock_usart_cr1 & STM32_USART_CR1_IDLEIE) != 0);
	TEST_ASSERT((mock_usart_cr1 & STM32_USART_CR1_RXNEIE) != 0);
	TEST_ASSERT((mock_usart_cr1 & STM32_USART_CR1_RE) != 0);

	/* 2. Feed valid 28-byte frame via RXNE interrupts followed by IDLE */
	uint8_t payload[24];
	for (size_t i = 0; i < sizeof(payload); i++)
		payload[i] = (uint8_t)(i + 0x10);
	uint8_t frame[SERVO_MAX_FRAME_BYTES];
	size_t frame_len = servo_frame_encode(SERVO_FRAME_DATA, sizeof(payload),
					      payload, frame);
	TEST_ASSERT(frame_len == 28);

	for (size_t i = 0; i < frame_len; i++) {
		mock_usart_feed_byte(frame[i]);
		usart_rx_interrupt_handler(&mock_usart4_config);
	}
	/* Assert IDLE */
	mock_usart_sr |= STM32_USART_SR_IDLE;
	usart_rx_interrupt_handler(&mock_usart4_config);

	TEST_ASSERT(!usart4_rx_corrupt_irq);
	TEST_ASSERT(queue_count(&usart4_to_bridge) == 28);

	uint8_t dequeued[28];
	for (size_t i = 0; i < 28; i++)
		TEST_ASSERT(queue_remove_unit(&usart4_to_bridge, &dequeued[i]));
	TEST_ASSERT(memcmp(dequeued, frame, 28) == 0);

	/* 3. Inject ORE (overrun) and BIT(1) (FE) mid-burst */
	drain_queue(&usart4_to_bridge);
	mock_usart_fifo_reset();
	usart4_rx_corrupt_irq = false;
	mock_usart4_state.rx_overrun = 0;

	for (int i = 0; i < 10; i++) {
		mock_usart_feed_byte(frame[i]);
		usart_rx_interrupt_handler(&mock_usart4_config);
	}
	/* Inject ORE */
	mock_usart_sr |= STM32_USART_SR_ORE;
	mock_usart_feed_byte(frame[10]);
	usart_rx_interrupt_handler(&mock_usart4_config);

	/* Assert IDLE */
	mock_usart_sr |= STM32_USART_SR_IDLE;
	usart_rx_interrupt_handler(&mock_usart4_config);

	TEST_ASSERT(queue_count(&usart4_to_bridge) == 0);
	TEST_ASSERT(mock_usart4_state.rx_overrun == 1);
	TEST_ASSERT(usart4_rx_corrupt_irq == true);

	/* Inject Framing Error BIT(1) */
	usart4_rx_corrupt_irq = false;
	mock_usart4_state.rx_overrun = 0;
	mock_usart_fifo_reset();

	for (int i = 0; i < 5; i++) {
		mock_usart_feed_byte(frame[i]);
		usart_rx_interrupt_handler(&mock_usart4_config);
	}
	mock_usart_sr |= BIT(1); /* FE */
	mock_usart_feed_byte(frame[5]);
	usart_rx_interrupt_handler(&mock_usart4_config);

	mock_usart_sr |= STM32_USART_SR_IDLE;
	usart_rx_interrupt_handler(&mock_usart4_config);

	TEST_ASSERT(queue_count(&usart4_to_bridge) == 0);
	TEST_ASSERT(mock_usart4_state.rx_overrun == 1);
	TEST_ASSERT(usart4_rx_corrupt_irq == true);

	/* 4. Inject queue-full shortfall (added < usart4_rx_len) on IDLE */
	usart4_rx_corrupt_irq = false;
	mock_usart4_state.rx_dropped = 0;
	mock_usart_fifo_reset();

	/* Fill queue until only 10 bytes remain */
	while (queue_space(&usart4_to_bridge) > 10) {
		uint8_t d = 0;
		queue_add_unit(&usart4_to_bridge, &d);
	}
	/* Stage 20 bytes into usart4_rx_buf */
	for (int i = 0; i < 20; i++) {
		mock_usart_feed_byte(0x55);
		usart_rx_interrupt_handler(&mock_usart4_config);
	}
	/* Trigger IDLE flush */
	mock_usart_sr |= STM32_USART_SR_IDLE;
	usart_rx_interrupt_handler(&mock_usart4_config);

	TEST_ASSERT(mock_usart4_state.rx_dropped == 10);
	TEST_ASSERT(usart4_rx_corrupt_irq == true);

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

	/* 1. Corrupt IRQ atomic flush */
	uint8_t garbage[16] = { 0xDE, 0xAD, 0xBE, 0xEF };
	queue_add_units(&usart4_to_bridge, garbage, sizeof(garbage));
	bridge.state = SERVO_STATE_ACTIVE;
	bridge.epoch = 1;
	bridge.rx.state = SERVO_RX_WAIT_PAYLOAD;
	usart4_rx_corrupt_irq = true;
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
	TEST_ASSERT((nak_frame[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_NAK);

	/* 2. frame_start_us re-anchoring */
	uint8_t single = SERVO_BRIDGE_SYNC;
	queue_add_unit(&usart4_to_bridge, &single);
	bridge.rx.state = SERVO_RX_WAIT_TYPE;
	bridge.rx.frame_start_us = 100;
	force_time((timestamp_t){ .val = 25000 });

	bridge_arbiter_deferred();
	TEST_ASSERT(bridge.rx.frame_start_us >= 25000 &&
		    bridge.rx.frame_start_us <= 25100);
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

	/* Verify forwarded DATA frame in bridge_to_usart4 has arg1 clamped to
	 * 16384 */
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
	TEST_ASSERT((cred[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_CREDIT);

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
	TEST_ASSERT((cred[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_CREDIT);

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
	TEST_ASSERT((mock_usb_ep[USB_EP_BRIDGE] & EP_TX_RX_MASK) ==
		    (EP_TX_STALL | EP_RX_STALL));

	/* 2. Fastboot ZLP emission */
	mock_pd_connected = true;
	active_usb_protocol = SERVO_PROTO_FASTBOOT;
	bridge.active_proto = SERVO_PROTO_FASTBOOT;
	bridge.state = SERVO_STATE_ACTIVE;
	bridge.epoch = 2;
	bridge.rx_seq = 0;
	bridge.usb_in_busy = false;
	bridge.tx_credits = 1;
	last_enumerated_proto = SERVO_PROTO_FASTBOOT;
	usb_bridge_pd_event();
	bridge_dppu_reconnect_deferred();

	TEST_ASSERT(effective_usb_protocol == SERVO_PROTO_FASTBOOT);
	TEST_ASSERT(mock_desc_patch[USB_DESC_BRIDGE] == 0x0342);

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
	TEST_ASSERT(baud_resp == DIV_ROUND_NEAREST(1000000 + 50, 100));

	/* USB_USART_SET_BAUD */
	rx_buf[0] = (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) |
		    (USB_USART_SET_BAUD << 8);
	rx_buf[1] = 0;
	rx_buf[2] = USB_IFACE_USART4_STREAM;
	rx_buf[3] = 0;
	ret = usart4_iface_request(rx_buf, tx_buf);
	TEST_ASSERT(ret == 0);

	/* USB_USART_CLEAR_QUEUES */
	kbd_in_flight = true;
	bridge.pending_key_cmd = true;
	rx_buf[0] = (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) |
		    (USB_USART_CLEAR_QUEUES << 8);
	rx_buf[1] = 0; /* CLEAR_BOTH_FIFOS */
	rx_buf[2] = USB_IFACE_USART4_STREAM;
	rx_buf[3] = 0;
	ret = usart4_iface_request(rx_buf, tx_buf);
	TEST_ASSERT(ret == 0);
	TEST_ASSERT(!kbd_in_flight);
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
		uint8_t epoch = (uint8_t)((f >> 1U) & SERVO_EPOCH_MASK);
		uint8_t type = (uint8_t)(SERVO_FRAME_DATA |
					 (seq << SERVO_DATA_SEQ_SHIFT) | epoch);
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
		(uint8_t)(SERVO_FRAME_DATA | (1U << SERVO_DATA_SEQ_SHIFT) | 2U),
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
	    p.type != (uint8_t)(SERVO_FRAME_DATA |
				(1U << SERVO_DATA_SEQ_SHIFT) | 2U) ||
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

/* 11. Test MSC Request Forwarding to EP2 IN & Unread Overwrite */
static int test_msc_req_forwards_to_ep2_in_and_overwrites_unread(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);
	mock_irq_depth_violation = false;
	mock_ep2_toggle_count = 0;

	usb_bridge_init();
	usb_msc_event(USB_EVENT_RESET);
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_TX_MASK) == EP_TX_NAK);
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_RX_MASK) == EP_RX_VALID);

	/* 1. ATmega sends SERVO_FRAME_MSC_REQ for Capacity query */
	struct servo_msc_req req1 = {
		.cmd = SERVO_MSC_CMD_CAP,
		.req_tag = 0x42,
		.media_gen = 1,
		.rsvd = 0,
		.lba_be = 0,
	};
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_MSC_REQ, sizeof(req1),
			 (const uint8_t *)&req1);
	bridge_arbiter_deferred();

	/* Verify EP2 IN armed with 8 bytes */
	TEST_ASSERT(btable_ep[USB_EP_MSC].tx_count == 8);
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_TX_MASK) == EP_TX_VALID);
	TEST_ASSERT(latest_msc_req_tag == 0x42);
	TEST_ASSERT(!mock_irq_depth_violation);
	TEST_ASSERT(mock_ep2_toggle_count > 0);

	struct servo_msc_req tx_buf;
	memcpy(&tx_buf, ep_msc_tx_buffer, sizeof(tx_buf));
	TEST_ASSERT(tx_buf.cmd == SERVO_MSC_CMD_CAP);
	TEST_ASSERT(tx_buf.req_tag == 0x42);
	TEST_ASSERT(tx_buf.media_gen == 1);

	/*
	 * 2. Unread overwrite test:
	 * Stage a pending MSC DATA frame with stale tag 0x42 in
	 * bridge.pending_msc_len.
	 */
	uint8_t stale_data[16];
	stale_data[0] = SERVO_MSC_RSP_CAP;
	stale_data[1] = 0x42;
	memset(&stale_data[2], 0x55, sizeof(stale_data) - 2);
	TEST_ASSERT(servo_bridge_send_msc_data(&bridge, stale_data,
					       sizeof(stale_data)));
	TEST_ASSERT(bridge.pending_msc_len == sizeof(stale_data));
	TEST_ASSERT(bridge.pending_msc_buf[1] == 0x42);

	/*
	 * ATmega sends a NEW SERVO_FRAME_MSC_REQ with tag 0x43
	 * (Host hasn't read req1 yet!).
	 */
	struct servo_msc_req req2 = {
		.cmd = SERVO_MSC_CMD_READ,
		.req_tag = 0x43,
		.media_gen = 1,
		.rsvd = 0,
		.lba_be = 0x00000100, /* LBA 256 */
	};
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_MSC_REQ, sizeof(req2),
			 (const uint8_t *)&req2);
	bridge_arbiter_deferred();

	/* Verify stale bridge.pending_msc_len was purged! */
	TEST_ASSERT(bridge.pending_msc_len == 0);
	TEST_ASSERT(latest_msc_req_tag == 0x43);

	/* Verify EP2 IN overwritten with req2 */
	TEST_ASSERT(btable_ep[USB_EP_MSC].tx_count == 8);
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_TX_MASK) == EP_TX_VALID);
	memcpy(&tx_buf, ep_msc_tx_buffer, sizeof(tx_buf));
	TEST_ASSERT(tx_buf.cmd == SERVO_MSC_CMD_READ);
	TEST_ASSERT(tx_buf.req_tag == 0x43);
	TEST_ASSERT(tx_buf.lba_be == 0x00000100);

	/* 3. Host reads EP2 IN: usb_msc_tx clears CTR_TX and leaves CTR_RX
	 * intact */
	mock_usb_ep[USB_EP_MSC] = EP_CTR_RX | EP_CTR_TX | EP_TX_VALID;
	mock_in_isr = true;
	usb_msc_tx();
	mock_in_isr = false;
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_CTR_TX) == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_CTR_RX) != 0);

	return EC_SUCCESS;
}

/* 12. Test EP2 OUT Stale-Tag Fast-Drain & Unconditional Availability */
static int test_msc_ep2_out_stale_tag_fast_drain(void)
{
	usb_bridge_init();
	bridge.state = SERVO_STATE_ACTIVE;
	bridge.pending_reset_req = false;
	bridge.epoch = 1;
	bridge.tx_credits = 4;
	pending_serial_sync = false;
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);

	/*
	 * 1. Verify Unconditional EP2 Availability when
	 * effective_usb_protocol == SERVO_PROTO_NONE.
	 */
	effective_usb_protocol = SERVO_PROTO_NONE;
	dppu_pulse_active = false;
	usb_msc_event(USB_EVENT_RESET);
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_TX_MASK) == EP_TX_NAK);
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_RX_MASK) == EP_RX_VALID);

	/*
	 * Interface SET_INTERFACE on USB_IFACE_MSC succeeds even when
	 * SERVO_PROTO_NONE.
	 */
	usb_uint rx_buf[4], tx_buf[4];
	rx_buf[0] = (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) |
		    (USB_REQ_SET_INTERFACE << 8);
	rx_buf[1] = 0;
	rx_buf[2] = USB_IFACE_MSC;
	rx_buf[3] = 0;
	mock_in_isr = true;
	int ret = msc_iface_request(rx_buf, tx_buf);
	mock_in_isr = false;
	TEST_ASSERT(ret == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_TX_MASK) == EP_TX_NAK);
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_RX_MASK) == EP_RX_VALID);

	/*
	 * CLEAR_FEATURE(ENDPOINT_HALT) on EP2 IN and OUT succeeds even when
	 * SERVO_PROTO_NONE.
	 */
	mock_in_isr = true;
	ret = usb_bridge_clear_halt(USB_EP_MSC | 0x80);
	TEST_ASSERT(ret == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_TX_MASK) == EP_TX_NAK);

	ret = usb_bridge_clear_halt(USB_EP_MSC);
	TEST_ASSERT(ret == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_RX_MASK) == EP_RX_VALID);
	mock_in_isr = false;

	/* 2. Stale-tag fast drain:
	 * latest_msc_req_tag is set to 0x77.
	 * Host writes a packet with tag 0x22 (stale tag).
	 */
	latest_msc_req_tag = 0x77;
	uint8_t stale_pkt[64];
	stale_pkt[0] = SERVO_MSC_RSP_CAP;
	stale_pkt[1] = 0x22; /* Mismatched tag */
	memset(&stale_pkt[2], 0xAA, sizeof(stale_pkt) - 2);

	memcpy(ep_msc_rx_buffer, stale_pkt, sizeof(stale_pkt));
	btable_ep[USB_EP_MSC].rx_count = sizeof(stale_pkt);
	mock_in_isr = true;
	usb_msc_rx();
	mock_in_isr = false;
	TEST_ASSERT(ep2_out_pending == 1);

	bridge_arbiter_deferred();

	/* Must be dropped immediately at USB speed:
	 * ep2_out_pending cleared, EP2 OUT re-armed with EP_RX_VALID,
	 * btable rx_count reset to 64, and ZERO bytes queued to usart4!
	 */
	TEST_ASSERT(ep2_out_pending == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_RX_MASK) == EP_RX_VALID);
	TEST_ASSERT(queue_is_empty(&bridge_to_usart4));
	TEST_ASSERT(bridge.pending_msc_len == 0);

	/* 3. Valid packet staging:
	 * Host writes a packet with matching tag 0x77.
	 */
	uint8_t valid_pkt[64];
	valid_pkt[0] = SERVO_MSC_RSP_CAP;
	valid_pkt[1] = 0x77; /* Matches latest_msc_req_tag */
	memset(&valid_pkt[2], 0xBB, sizeof(valid_pkt) - 2);

	memcpy(ep_msc_rx_buffer, valid_pkt, sizeof(valid_pkt));
	btable_ep[USB_EP_MSC].rx_count = sizeof(valid_pkt);
	mock_in_isr = true;
	usb_msc_rx();
	mock_in_isr = false;
	TEST_ASSERT(ep2_out_pending == 1);

	bridge_arbiter_deferred();

	/* Must be staged into bridge and re-armed */
	TEST_ASSERT(ep2_out_pending == 0);
	TEST_ASSERT((mock_usb_ep[USB_EP_MSC] & EP_RX_MASK) == EP_RX_VALID);

	/* Popped into bridge_to_usart4 as SERVO_FRAME_MSC_DATA (0x90) */
	TEST_ASSERT(!queue_is_empty(&bridge_to_usart4));
	uint8_t out_frame[SERVO_MAX_FRAME_BYTES];
	int out_len = 0;
	while (queue_remove_unit(&bridge_to_usart4, &out_frame[out_len]))
		out_len++;

	TEST_ASSERT(out_len == (int)(4 + sizeof(valid_pkt)));
	TEST_ASSERT((out_frame[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_MSC_DATA);
	TEST_ASSERT(out_frame[2] == sizeof(valid_pkt));
	TEST_ASSERT(out_frame[3] == SERVO_MSC_RSP_CAP);
	TEST_ASSERT(out_frame[4] == 0x77);

	return EC_SUCCESS;
}

/* 13. Test Priority 3A/3B 1-for-1 Round-Robin Fairness */
static int test_msc_and_adb_round_robin_fairness(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);

	bridge.state = SERVO_STATE_ACTIVE;
	bridge.epoch = 1;
	bridge.active_proto = SERVO_PROTO_ADB;
	bridge.tx_credits = 4;
	bridge.last_bulk_was_msc = false;

	uint8_t adb_payload[64];
	memset(adb_payload, 0x11, sizeof(adb_payload));
	uint8_t msc_payload[64];
	memset(msc_payload, 0x22, sizeof(msc_payload));

	/* Stage ADB data */
	bridge.has_tx_data = true;
	bridge.tx_data_seq = 0;
	bridge.tx_data_len = sizeof(adb_payload);
	memcpy(bridge.tx_data_buf, adb_payload, sizeof(adb_payload));
	bridge.pending_data_tx = true;

	/* Stage MSC data */
	TEST_ASSERT(servo_bridge_send_msc_data(&bridge, msc_payload,
					       sizeof(msc_payload)));

	/*
	 * Both ready! Since !bridge.last_bulk_was_msc, Arbiter MUST
	 * pick MSC (Priority 3B).
	 */
	uint8_t frame[SERVO_MAX_FRAME_BYTES];
	uint32_t now_us = (uint32_t)get_time().val;
	uint8_t len = servo_bridge_pop_tx_frame(&bridge, 128, frame, now_us);
	TEST_ASSERT(len == (size_t)(4 + sizeof(msc_payload)));
	TEST_ASSERT((frame[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_MSC_DATA);
	TEST_ASSERT(bridge.last_bulk_was_msc == true);

	/* Re-stage MSC data so both are ready again */
	TEST_ASSERT(servo_bridge_send_msc_data(&bridge, msc_payload,
					       sizeof(msc_payload)));

	/*
	 * Since bridge.last_bulk_was_msc == true, Arbiter MUST pick ADB
	 * (Priority 3A).
	 */
	len = servo_bridge_pop_tx_frame(&bridge, 128, frame, now_us);
	TEST_ASSERT(len == (size_t)(4 + sizeof(adb_payload)));
	TEST_ASSERT((frame[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_DATA);
	TEST_ASSERT(bridge.last_bulk_was_msc == false);

	/* Now only MSC remains */
	len = servo_bridge_pop_tx_frame(&bridge, 128, frame, now_us);
	TEST_ASSERT(len == (size_t)(4 + sizeof(msc_payload)));
	TEST_ASSERT((frame[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_MSC_DATA);
	TEST_ASSERT(bridge.last_bulk_was_msc == true);

	return EC_SUCCESS;
}

/* 14. Test Servo Serial Sync on Boot Handshake */
static int test_msc_serial_sync_on_boot_handshake(void)
{
	drain_queue(&usart4_to_bridge);
	drain_queue(&bridge_to_usart4);

	mock_serial_str = "SN-SERVO-999"; /* 12 characters */
	usb_bridge_init();

	/* Initial state: pending_serial_sync is true from init */
	TEST_ASSERT(pending_serial_sync == true);

	/* Bring bridge to ACTIVE */
	bridge.state = SERVO_STATE_ACTIVE;
	bridge.pending_reset_req = false;
	bridge.epoch = 1;
	bridge.active_proto = SERVO_PROTO_ADB;
	bridge.tx_credits = 2;
	bridge.pending_msc_len = 0;
	drain_queue(&bridge_to_usart4);

	bridge_arbiter_deferred();

	/* Verify serial sync queued and sent */
	TEST_ASSERT(pending_serial_sync == false);
	TEST_ASSERT(!queue_is_empty(&bridge_to_usart4));

	uint8_t out_frame[SERVO_MAX_FRAME_BYTES];
	int out_len = 0;
	while (queue_remove_unit(&bridge_to_usart4, &out_frame[out_len]))
		out_len++;

	/*
	 * Frame format: [0]=SYNC (0xA5), [1]=TYPE (0x90),
	 * [2]=ARG_LEN (16), [3..18]=payload, [19]=crc8.
	 */
	TEST_ASSERT(out_len == 4 + 16);
	TEST_ASSERT((out_frame[1] & SERVO_OPCODE_MASK) == SERVO_FRAME_MSC_DATA);
	TEST_ASSERT(out_frame[2] == 16);
	TEST_ASSERT(out_frame[3] == SERVO_MSC_RSP_SERIAL); /* 'S' */
	TEST_ASSERT(out_frame[4] == 0);
	TEST_ASSERT(out_frame[5] == 12); /* slen */
	TEST_ASSERT(out_frame[6] == 0);
	TEST_ASSERT(memcmp(&out_frame[7], "SN-SERVO-999", 12) == 0);

	/*
	 * Verify pending_serial_sync is held while bridge is in
	 * SERVO_STATE_RESETTING.
	 */
	drain_queue(&bridge_to_usart4);
	pending_serial_sync = true;
	bridge.state = SERVO_STATE_RESETTING;
	bridge_arbiter_deferred();
	TEST_ASSERT(pending_serial_sync == true);
	TEST_ASSERT(queue_is_empty(&bridge_to_usart4));

	/* When bridge becomes ACTIVE, pending serial sync is sent */
	bridge.state = SERVO_STATE_ACTIVE;
	bridge_arbiter_deferred();
	TEST_ASSERT(pending_serial_sync == false);
	TEST_ASSERT(!queue_is_empty(&bridge_to_usart4));

	/* Verify re-trigger on boot RESET_ACK */
	drain_queue(&bridge_to_usart4);
	pending_serial_sync = false;
	bridge.state = SERVO_STATE_RESETTING;
	queue_feed_frame(&usart4_to_bridge, SERVO_FRAME_RESET_ACK | 0,
			 SERVO_RESET_FLAG_BOOT | SERVO_PROTO_NONE, NULL);
	bridge_arbiter_deferred();
	/* Boot handshake completed; bridge active -> serial sync sent! */
	TEST_ASSERT(pending_serial_sync == false);
	TEST_ASSERT(!queue_is_empty(&bridge_to_usart4));
	out_len = 0;
	while (queue_remove_unit(&bridge_to_usart4, &out_frame[out_len]))
		out_len++;
	TEST_ASSERT(out_len == 4 + 16);
	TEST_ASSERT(out_frame[3] == SERVO_MSC_RSP_SERIAL);

	/* Verify default uninitialized serial number does not queue sync packet
	 */
	drain_queue(&bridge_to_usart4);
	mock_serial_str = "Uninitialized";
	bridge.state = SERVO_STATE_ACTIVE;
	bridge.pending_msc_len = 0;
	bridge_arbiter_deferred();
	TEST_ASSERT(pending_serial_sync == false);
	TEST_ASSERT(queue_is_empty(&bridge_to_usart4));

	/* Verify NULL serial does not crash or queue packet */
	mock_serial_str = NULL;
	pending_serial_sync = true;
	bridge_arbiter_deferred();
	TEST_ASSERT(pending_serial_sync == false);
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
	RUN_TEST(test_msc_req_forwards_to_ep2_in_and_overwrites_unread);
	RUN_TEST(test_msc_ep2_out_stale_tag_fast_drain);
	RUN_TEST(test_msc_and_adb_round_robin_fairness);
	RUN_TEST(test_msc_serial_sync_on_boot_handshake);

	test_print_result();
}
