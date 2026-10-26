/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Canonical Inter-MCU Framed UART Protocol & Unified TLA+ State Machine
 * for the Servo V4.1 USB Host-to-USB Host ADB/Fastboot Bridge.
 *
 * Shared verbatim across:
 *   1. STM32F072 (U20, ARM Cortex-M0, ec-legacy: board/servo_v4p1/usb_bridge.c)
 *   2. ATmega32U4 (U13, 8-bit AVR, hdctools: UsbBridge/UsbBridge.c)
 *   3. Host Verification & Lockstep TLA+ Refinement Suite
 *      (servo_usb_bridge_frame_test.c)
 *
 * Formal Specification Reference: protocol/ServoUsbBridge.tla
 */

#ifndef __CROS_EC_SERVO_USB_BRIDGE_FRAME_H
#define __CROS_EC_SERVO_USB_BRIDGE_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. Wire Frame Constants, Opcodes, & Protocol Identifiers
 * ========================================================================= */

#define SERVO_BRIDGE_SYNC 0xA5U

/* Channel & Opcode bit shifts & masks in TYPE byte: [CHAN:3][OP:3][SEQ/EPOCH:2]
 */
#define SERVO_CHAN_SHIFT 5U
#define SERVO_CHAN_MASK 0xE0U
#define SERVO_OP_SHIFT 2U
#define SERVO_OP_MASK 0x1CU
#define SERVO_SEQ_MASK 0x03U
#define SERVO_EPOCH_MASK 0x03U

/* Channels (3 bits: 0..7) */
#define SERVO_CHAN_SYS 0U /* 0x00..0x1F: MCU-wide Cold-Boot Reset */
#define SERVO_CHAN_H2H \
	1U /* 0x20..0x3F: ADB (0x01) / Fastboot (0x03) Bulk Bridge */
#define SERVO_CHAN_MSC                                         \
	2U /* 0x40..0x5F: USB Mass Storage BOT (Interface 0/1) \
	    */
#define SERVO_CHAN_KBD 3U /* 0x60..0x7F: USB HID Keyboard (usbkm232 EP5) */
#define SERVO_CHAN_COUNT 4U

/* Opcodes (3 bits: 0..7) */
#define SERVO_OP_DATA 0U /* 0b000: Data frame */
#define SERVO_OP_CREDIT 1U /* 0b001: 4B Flow-control credit */
#define SERVO_OP_WAIT 2U /* 0b010: 4B Link-ACK while USB IN endpoint busy */
#define SERVO_OP_NAK 3U /* 0b011: 4B Negative ACK on CRC/timeout */
#define SERVO_OP_RESET_REQ 4U /* 0b100: 4B Channel/Link Reset Request */
#define SERVO_OP_RESET_ACK 5U /* 0b101: 4B Channel/Link Reset Acknowledge */

#define SERVO_MAKE_TYPE(ch, op, seq)                               \
	((uint8_t)((((uint8_t)(ch) & 0x07U) << SERVO_CHAN_SHIFT) | \
		   (((uint8_t)(op) & 0x07U) << SERVO_OP_SHIFT) |   \
		   ((uint8_t)(seq) & SERVO_SEQ_MASK)))

#define SERVO_GET_CHAN(type) \
	((uint8_t)(((uint8_t)(type) & SERVO_CHAN_MASK) >> SERVO_CHAN_SHIFT))
#define SERVO_GET_OP(type) \
	((uint8_t)(((uint8_t)(type) & SERVO_OP_MASK) >> SERVO_OP_SHIFT))
#define SERVO_GET_SEQ(type) ((uint8_t)((uint8_t)(type) & SERVO_SEQ_MASK))

/* 2-bit modulo-4 directional epoch arithmetic */
#define SERVO_NEXT_EPOCH(e) (((uint8_t)(e) + 1U) & SERVO_EPOCH_MASK)
#define SERVO_IS_NEWER_EPOCH(r, c) \
	((((uint8_t)(r) - (uint8_t)(c)) & SERVO_EPOCH_MASK) == 1U)

/* RESET_REQ / RESET_ACK ARG byte encoding */
#define SERVO_RESET_PROTO_MASK 0x03U
#define SERVO_RESET_FLAG_PEER_KICK 0x20U
#define SERVO_RESET_FLAG_HOST_ACTIVE 0x40U
#define SERVO_RESET_VALID_ARG_MASK                                   \
	(SERVO_RESET_FLAG_HOST_ACTIVE | SERVO_RESET_FLAG_PEER_KICK | \
	 SERVO_RESET_PROTO_MASK)

/* Bridged USB Protocols (ATmega32U4 is Protocol Authority) */
#define SERVO_PROTO_NONE 0x00U
#define SERVO_PROTO_ADB 0x01U
#define SERVO_PROTO_FASTBOOT 0x03U

/* Maximum UART frame payload & total wire frame length */
#define SERVO_MAX_DATA_LEN 64U
#define SERVO_MAX_PAYLOAD SERVO_MAX_DATA_LEN
#define SERVO_MAX_FRAME_BYTES (4U + SERVO_MAX_DATA_LEN)
#define SERVO_TX_RING_BYTES 128U

/*
 * 24B (6 x 4B) reserved in 128B UART TX buffer for all Priority 1 control types
 */
#define SERVO_CTRL_TX_HEADROOM 24U
#define SERVO_KEY_TX_MIN_FREE (4U + SERVO_CTRL_TX_HEADROOM)
#define SERVO_DATA_TX_MIN_FREE(len) \
	((size_t)(4U + (size_t)(len) + SERVO_CTRL_TX_HEADROOM))

/* In-flight ADB A_CNXN max_payload clamp (16 KiB = ~200 ms at 1.000 Mbps) */
#define SERVO_MAX_PAYLOAD_CLAMP 16384U

/* Protocol Timers (microseconds) */
#define SERVO_RETRANSMIT_TIMEOUT_US \
	15000U /* 15 ms T_RET retransmission timer */
#define SERVO_WAIT_KEEPALIVE_TIMEOUT_US \
	250000U /* 250 ms fallback keepalive when tx_link_acked == true */
#define SERVO_RX_FRAME_TIMEOUT_US \
	10000U /* 10 ms inter-byte gap T_RX_FRAME deadline */
#define SERVO_KBD_CREDIT_TIMEOUT_US 50000U /* 50 ms KBD credit restoration */
#define SERVO_RESET_RETRY_US 5000U /* 5 ms reset retry timer */

/* ADB A_CNXN wire constants (little-endian) */
#define SERVO_ADB_CNXN_CMD 0x4E584E43UL /* "CNXN" */
#define SERVO_ADB_CNXN_MAGIC 0xB1A7B1BCUL /* A_CNXN ^ 0xFFFFFFFF */
#define SERVO_ADB_MSG_HEADER_LEN 24U

static inline bool servo_is_valid_proto(uint8_t proto)
{
	return proto == SERVO_PROTO_NONE || proto == SERVO_PROTO_ADB ||
	       proto == SERVO_PROTO_FASTBOOT;
}

static inline bool servo_is_duplicate_data(uint8_t frame_seq, uint8_t rx_seq)
{
	return (frame_seq & SERVO_SEQ_MASK) != (rx_seq & SERVO_SEQ_MASK);
}

/* =========================================================================
 * 2. CRC-8-CCITT (poly = 0x07, init = 0x00, MSB-first, no final XOR)
 * ========================================================================= */

#if defined(__GNUC__) && !defined(__AVR__)
#define SERVO_HELPER static __attribute__((noinline, unused))
#else
#define SERVO_HELPER static inline
#endif

SERVO_HELPER uint8_t servo_crc8_ccitt(uint8_t crc, uint8_t data)
{
	uint8_t i;

	crc ^= data;
	for (i = 0U; i < 8U; ++i) {
		if ((crc & 0x80U) != 0U)
			crc = (uint8_t)((crc << 1U) ^ 0x07U);
		else
			crc = (uint8_t)(crc << 1U);
	}
	return crc;
}

static inline void servo_frame_encode_header(uint8_t type, uint8_t arg_len,
					     uint8_t out_hdr[3],
					     uint8_t *crc_out)
{
	uint8_t crc;

	out_hdr[0] = SERVO_BRIDGE_SYNC;
	out_hdr[1] = type;
	out_hdr[2] = arg_len;
	crc = servo_crc8_ccitt(0x00U, type);
	crc = servo_crc8_ccitt(crc, arg_len);
	if (crc_out != NULL)
		*crc_out = crc;
}

static inline size_t
servo_frame_encode(uint8_t type, uint8_t arg_len, const uint8_t *payload,
		   uint8_t out_frame[SERVO_MAX_FRAME_BYTES])
{
	uint8_t crc = 0U;
	uint8_t i;
	uint8_t ch = SERVO_GET_CHAN(type);
	uint8_t op = SERVO_GET_OP(type);
	bool has_data = (ch == SERVO_CHAN_H2H && op == SERVO_OP_DATA);
	uint8_t len = has_data ? ((arg_len <= SERVO_MAX_DATA_LEN) ?
					  arg_len :
					  SERVO_MAX_DATA_LEN) :
				 0U;
	uint8_t hdr_arg = has_data ? len : arg_len;

	servo_frame_encode_header(type, hdr_arg, out_frame, &crc);
	for (i = 0U; i < len; ++i) {
		uint8_t b = (payload != NULL) ? payload[i] : 0U;
		out_frame[3U + i] = b;
		crc = servo_crc8_ccitt(crc, b);
	}
	out_frame[3U + len] = crc;
	return (size_t)(4U + len);
}

/* =========================================================================
 * 3. Byte-Wise Little-Endian A_CNXN Detection & max_payload Clamping
 * ========================================================================= */

#ifndef __AVR__
static inline uint32_t servo_read_u32_le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
	       ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static inline void servo_write_u32_le(uint8_t *p, uint32_t val)
{
	p[0] = (uint8_t)(val & 0xFFU);
	p[1] = (uint8_t)((val >> 8U) & 0xFFU);
	p[2] = (uint8_t)((val >> 16U) & 0xFFU);
	p[3] = (uint8_t)((val >> 24U) & 0xFFU);
}

SERVO_HELPER bool servo_is_adb_cnxn(const uint8_t *pkt, uint8_t len,
				    uint8_t active_proto)
{
	if (pkt == NULL || active_proto != SERVO_PROTO_ADB ||
	    len != SERVO_ADB_MSG_HEADER_LEN)
		return false;

	return servo_read_u32_le(&pkt[0]) == SERVO_ADB_CNXN_CMD &&
	       servo_read_u32_le(&pkt[20]) == SERVO_ADB_CNXN_MAGIC;
}

SERVO_HELPER void servo_clamp_adb_cnxn(uint8_t *pkt, uint8_t len,
				       uint8_t active_proto)
{
	uint32_t arg1;

	if (!servo_is_adb_cnxn(pkt, len, active_proto))
		return;

	arg1 = servo_read_u32_le(&pkt[8]);
	if (arg1 > SERVO_MAX_PAYLOAD_CLAMP)
		servo_write_u32_le(&pkt[8], SERVO_MAX_PAYLOAD_CLAMP);
}
#endif /* !__AVR__ */

/* =========================================================================
 * 4. Byte-by-Byte RX Frame Parser with Whole-Frame Timeout & Pre-CRC Guards
 * ========================================================================= */

enum servo_rx_state {
	SERVO_RX_WAIT_SYNC = 0,
	SERVO_RX_WAIT_TYPE = 1,
	SERVO_RX_WAIT_ARG_LEN = 2,
	SERVO_RX_WAIT_PAYLOAD = 3,
	SERVO_RX_WAIT_CRC = 4,
};

enum servo_rx_event {
	SERVO_RX_EVT_NONE = 0,
	SERVO_RX_EVT_FRAME_COMPLETE = 1,
	SERVO_RX_EVT_CRC_ERROR = 2,
	SERVO_RX_EVT_TIMEOUT_ERROR = 3,
	SERVO_RX_EVT_INVALID_HEADER = 4,
};

struct servo_rx_parser {
	uint8_t state;
	uint8_t type;
	uint8_t arg_len;
	uint8_t payload[SERVO_MAX_DATA_LEN];
	uint8_t bytes_read;
	uint8_t running_crc;
	uint32_t frame_start_us;
	bool timeout_occurred;
};

static inline void servo_frame_rx_init(struct servo_rx_parser *p)
{
	p->state = SERVO_RX_WAIT_SYNC;
	p->type = 0U;
	p->arg_len = 0U;
	p->bytes_read = 0U;
	p->running_crc = 0U;
	p->frame_start_us = 0U;
	p->timeout_occurred = false;
}

static inline bool servo_frame_is_valid_type(uint8_t type)
{
	uint8_t ch = SERVO_GET_CHAN(type);
	uint8_t op = SERVO_GET_OP(type);

	switch (ch) {
	case SERVO_CHAN_SYS:
		return (op == SERVO_OP_RESET_REQ || op == SERVO_OP_RESET_ACK);
	case SERVO_CHAN_H2H:
		return (op <= SERVO_OP_RESET_ACK);
	case SERVO_CHAN_KBD:
		return (op == SERVO_OP_DATA || op == SERVO_OP_CREDIT);
	case SERVO_CHAN_MSC:
		/* In Commit A: MSC is inactive and rejected */
		return false;
	default:
		return false;
	}
}

static inline bool servo_frame_is_valid_arg(uint8_t type, uint8_t arg_len)
{
	uint8_t ch = SERVO_GET_CHAN(type);
	uint8_t op = SERVO_GET_OP(type);

	switch (ch) {
	case SERVO_CHAN_SYS:
		return ((arg_len & ~SERVO_RESET_VALID_ARG_MASK) == 0U &&
			(arg_len & SERVO_RESET_PROTO_MASK) != 0x02U);
	case SERVO_CHAN_H2H:
		switch (op) {
		case SERVO_OP_DATA:
			return arg_len <= SERVO_MAX_DATA_LEN;
		case SERVO_OP_CREDIT:
		case SERVO_OP_WAIT:
		case SERVO_OP_NAK:
			return (arg_len & ~SERVO_SEQ_MASK) == 0U;
		case SERVO_OP_RESET_REQ:
		case SERVO_OP_RESET_ACK:
			return ((arg_len & ~SERVO_RESET_VALID_ARG_MASK) == 0U &&
				(arg_len & SERVO_RESET_PROTO_MASK) != 0x02U);
		default:
			return false;
		}
	case SERVO_CHAN_KBD:
		return true;
	case SERVO_CHAN_MSC:
		return false;
	default:
		return false;
	}
}

static inline bool servo_frame_is_valid_header(uint8_t type, uint8_t arg_len)
{
	return servo_frame_is_valid_type(type) &&
	       servo_frame_is_valid_arg(type, arg_len);
}

static inline bool servo_frame_is_valid_type_and_arg(uint8_t type, uint8_t arg)
{
	return servo_frame_is_valid_header(type, arg);
}

static inline uint8_t servo_frame_payload_len(uint8_t type, uint8_t arg)
{
	uint8_t ch = SERVO_GET_CHAN(type);
	uint8_t op = SERVO_GET_OP(type);

	if (ch == SERVO_CHAN_H2H && op == SERVO_OP_DATA)
		return (arg <= SERVO_MAX_DATA_LEN) ? arg : SERVO_MAX_DATA_LEN;

	return 0U;
}

SERVO_HELPER enum servo_rx_event
servo_frame_rx_check_timeout(struct servo_rx_parser *p, uint32_t now_us)
{
	if (p->state != SERVO_RX_WAIT_SYNC &&
	    (uint32_t)(now_us - p->frame_start_us) >=
		    SERVO_RX_FRAME_TIMEOUT_US) {
		p->state = SERVO_RX_WAIT_SYNC;
		p->bytes_read = 0U;
		p->running_crc = 0U;
		return SERVO_RX_EVT_TIMEOUT_ERROR;
	}
	return SERVO_RX_EVT_NONE;
}

static inline enum servo_rx_event
servo_frame_rx_step(struct servo_rx_parser *p, uint8_t byte, uint32_t now_us)
{
	p->timeout_occurred = false;

	if (servo_frame_rx_check_timeout(p, now_us) ==
	    SERVO_RX_EVT_TIMEOUT_ERROR) {
		p->timeout_occurred = true;
		if (byte == SERVO_BRIDGE_SYNC) {
			p->state = SERVO_RX_WAIT_TYPE;
			p->frame_start_us = now_us;
			return SERVO_RX_EVT_NONE;
		}
		return SERVO_RX_EVT_TIMEOUT_ERROR;
	}

	switch (p->state) {
	case SERVO_RX_WAIT_SYNC:
		if (byte == SERVO_BRIDGE_SYNC) {
			p->state = SERVO_RX_WAIT_TYPE;
			p->frame_start_us = now_us;
			p->bytes_read = 0U;
			p->running_crc = 0U;
		}
		return SERVO_RX_EVT_NONE;

	case SERVO_RX_WAIT_TYPE:
		if (byte == SERVO_BRIDGE_SYNC) {
			p->frame_start_us = now_us;
			return SERVO_RX_EVT_NONE;
		}
		if (!servo_frame_is_valid_type(byte)) {
			p->state = SERVO_RX_WAIT_SYNC;
			return SERVO_RX_EVT_INVALID_HEADER;
		}
		p->type = byte;
		p->running_crc = servo_crc8_ccitt(0x00U, byte);
		p->state = SERVO_RX_WAIT_ARG_LEN;
		p->frame_start_us = now_us;
		return SERVO_RX_EVT_NONE;

	case SERVO_RX_WAIT_ARG_LEN:
		if (!servo_frame_is_valid_header(p->type, byte)) {
			p->state = SERVO_RX_WAIT_SYNC;
			return SERVO_RX_EVT_INVALID_HEADER;
		}
		p->arg_len = byte;
		p->running_crc = servo_crc8_ccitt(p->running_crc, byte);
		p->bytes_read = 0U;
		if (servo_frame_payload_len(p->type, byte) > 0U) {
			p->state = SERVO_RX_WAIT_PAYLOAD;
		} else {
			p->state = SERVO_RX_WAIT_CRC;
		}
		p->frame_start_us = now_us;
		return SERVO_RX_EVT_NONE;

	case SERVO_RX_WAIT_PAYLOAD:
		p->payload[p->bytes_read++] = byte;
		p->running_crc = servo_crc8_ccitt(p->running_crc, byte);
		if (p->bytes_read >= p->arg_len)
			p->state = SERVO_RX_WAIT_CRC;
		p->frame_start_us = now_us;
		return SERVO_RX_EVT_NONE;

	case SERVO_RX_WAIT_CRC:
		p->state = SERVO_RX_WAIT_SYNC;
		p->frame_start_us = now_us;
		if (byte == p->running_crc)
			return SERVO_RX_EVT_FRAME_COMPLETE;
		return SERVO_RX_EVT_CRC_ERROR;

	default:
		p->state = SERVO_RX_WAIT_SYNC;
		return SERVO_RX_EVT_INVALID_HEADER;
	}
}

/* =========================================================================
 * 5. Unified Freestanding C99 Protocol State Machine (ServoUsbBridge.tla)
 * ========================================================================= */

enum servo_node_state {
	SERVO_STATE_RESETTING = 0,
	SERVO_STATE_ACTIVE = 1,
};

struct servo_bridge_rx_effects {
	bool usb_in_armed;
	uint8_t usb_in_len;
	uint8_t usb_in_payload[SERVO_MAX_DATA_LEN];
	bool key_cmd_received;
	uint8_t key_cmd_ch;
	bool key_ack_received;
	uint8_t key_ack_ch;
	bool proto_changed;
	uint8_t new_active_proto;

	/* Host C-to-TLA+ refinement oracle hooks */
	bool epoch_history_cleared;
	uint8_t cleared_epoch;
	bool reset_occurred;
	bool peer_kick_received;
	bool arm_reset_zlp;
};

struct servo_bridge_node {
	/* Timestamps grouped at top to eliminate alignment padding on 32-bit
	 * ARM */
	uint32_t last_tx_us;
	uint32_t last_reset_tx_us;
	uint32_t last_kbd_tx_us;

	/* Role: true = STM32 (Epoch Leader), false = ATmega (Authority) */
	bool is_epoch_leader;

	/* Core TLA+ node state variables */
	enum servo_node_state state;
	uint8_t epoch;
	bool booted;
	uint8_t active_proto;
	uint8_t desired_proto;
	uint8_t pre_suspend_proto;

	/* Stop-and-wait flow control & outbound USB Bulk IN backpressure */
	uint8_t tx_seq;
	uint8_t rx_seq;
	uint8_t tx_credits;
	bool usb_in_busy;
	bool credit_pending;
	bool tx_link_acked;

	/* Host active and peer kick flags */
	bool local_host_active;
	bool send_peer_kick;

#ifndef __AVR__
	/*
	 * ADB session resynchronization & active A_CNXN response coalescing
	 * state (ServoAdbSession.tla). Used by STM32 (is_epoch_leader == true)
	 * in SERVO_PROTO_ADB.
	 */
	bool adb_awaiting_cnxn;
	uint8_t adb_pending_syncs;
	uint16_t adb_cnxn_req_rem_chunks;
	uint16_t adb_cnxn_resp_rem_chunks;
	uint16_t adb_cnxn_drop_out_chunks;
#endif

	/* Outbound DATA retransmission buffer (tx_buf in ServoUsbBridge.tla) */
	bool has_tx_data;
	uint8_t tx_data_seq;
	uint8_t tx_data_len;
	uint8_t tx_data_buf[SERVO_MAX_DATA_LEN];

	/* Keyboard hop-by-hop link flow control */
	uint8_t kbd_tx_credits;
	uint8_t kbd_tx_seq;
	bool pending_key_cmd;
	uint8_t pending_key_cmd_ch;
	bool pending_key_ack;
	uint8_t pending_key_ack_seq;
	uint8_t pending_key_ack_ch;

	/* Coalesced Priority 1 / Priority 2 / Priority 3 TX pending flags */
	bool pending_reset_req;

	bool pending_reset_ack;
	uint8_t pending_reset_ack_epoch;
	bool pending_reset_ack_boot;
	uint8_t pending_reset_ack_proto;

	bool pending_credit;
	uint8_t pending_credit_epoch;
	uint8_t pending_credit_seq;

	bool pending_wait_ack;
	uint8_t pending_wait_seq;

	bool pending_nak;
	uint8_t pending_nak_epoch;
	uint8_t pending_nak_seq;

	bool pending_data_tx;

	/* Byte-stream RX parser */
	struct servo_rx_parser rx;
};

static inline bool servo_is_epoch_leader(const struct servo_bridge_node *n)
{
#if defined(SERVO_BRIDGE_FIXED_LEADER)
	(void)n;
	return SERVO_BRIDGE_FIXED_LEADER != 0;
#elif defined(__AVR__)
	(void)n;
	return false;
#else
	return n->is_epoch_leader;
#endif
}

static inline void servo_adb_session_reset(struct servo_bridge_node *n)
{
#ifndef __AVR__
	n->adb_awaiting_cnxn = true;
	n->adb_pending_syncs = 0U;
	n->adb_cnxn_req_rem_chunks = 0U;
	n->adb_cnxn_resp_rem_chunks = 0U;
	n->adb_cnxn_drop_out_chunks = 0U;
#else
	(void)n;
#endif
}

static inline void servo_bridge_clear_tx_buf(struct servo_bridge_node *n)
{
	n->has_tx_data = false;
	n->tx_data_seq = 0U;
	n->tx_data_len = 0U;
	n->pending_data_tx = false;
}

static inline void servo_bridge_init(struct servo_bridge_node *n,
				     bool is_epoch_leader, uint32_t now_us)
{
	memset(n, 0, sizeof(*n));
	servo_adb_session_reset(n);
	n->last_tx_us = now_us;
	n->last_reset_tx_us = now_us;
	n->last_kbd_tx_us = now_us;

	n->is_epoch_leader = is_epoch_leader;
	n->state = SERVO_STATE_RESETTING;
	n->booted = true;
	n->kbd_tx_credits = 1U;
	n->pending_reset_req = true;
}

#define servo_bridge_node_init servo_bridge_init

SERVO_HELPER void
servo_bridge_apply_h2h_reset(struct servo_bridge_node *n, uint8_t epoch,
			     uint8_t credits, uint8_t proto,
			     struct servo_bridge_rx_effects *fx)
{
#ifndef __AVR__
	bool had_adb = !n->adb_awaiting_cnxn;
#endif
	n->epoch = epoch;
	n->tx_credits = credits;
	n->tx_seq = 0U;
	n->rx_seq = 0U;
	n->usb_in_busy = false;
	n->credit_pending = false;
	n->tx_link_acked = false;
	n->pending_wait_ack = false;
	n->pending_wait_seq = 0U;
	servo_adb_session_reset(n);
	servo_bridge_clear_tx_buf(n);
	n->active_proto = proto;
	if (fx != NULL) {
		fx->epoch_history_cleared = true;
		fx->cleared_epoch = epoch;
		fx->reset_occurred = true;
#ifndef __AVR__
		if (had_adb)
			fx->arm_reset_zlp = true;
#endif
	}
}

static inline void servo_bridge_trigger_reset(struct servo_bridge_node *n,
					      uint32_t now_us)
{
	uint8_t next_epoch = (n->state == SERVO_STATE_ACTIVE) ?
				     SERVO_NEXT_EPOCH(n->epoch) :
				     n->epoch;
	bool next_boot = (n->state == SERVO_STATE_ACTIVE) ? false : n->booted;

	n->state = SERVO_STATE_RESETTING;
	n->booted = next_boot;
	servo_bridge_apply_h2h_reset(n, next_epoch, 0U, n->active_proto, NULL);
	n->pending_credit = false;
	n->pending_nak = false;
	n->pending_reset_req = true;
	n->last_tx_us = now_us;
	n->last_reset_tx_us = now_us;
}

static inline bool servo_bridge_set_proto(struct servo_bridge_node *n,
					  uint8_t proto, uint32_t now_us)
{
	if (servo_is_epoch_leader(n) || n->state != SERVO_STATE_ACTIVE ||
	    !servo_is_valid_proto(proto) ||
	    (n->active_proto == proto && proto == SERVO_PROTO_NONE))
		return false;

	n->pre_suspend_proto = SERVO_PROTO_NONE;
	n->active_proto = proto;
	n->desired_proto = proto;
	servo_bridge_trigger_reset(n, now_us);
	return true;
}

static inline bool servo_bridge_request_proto(struct servo_bridge_node *n,
					      uint8_t proto, uint32_t now_us)
{
	if (servo_is_epoch_leader(n) || !servo_is_valid_proto(proto))
		return false;

	n->pre_suspend_proto = SERVO_PROTO_NONE;
	if (n->state == SERVO_STATE_RESETTING && n->active_proto == proto &&
	    n->desired_proto == proto) {
		n->usb_in_busy = false;
		return true;
	}
	n->desired_proto = proto;
	if (n->booted) {
		n->active_proto = proto;
		n->pending_reset_req = true;
		n->last_reset_tx_us = now_us;
		n->usb_in_busy = false;
		return true;
	}
	if (n->state == SERVO_STATE_ACTIVE &&
	    (n->active_proto != proto || proto != SERVO_PROTO_NONE))
		return servo_bridge_set_proto(n, proto, now_us);
	n->usb_in_busy = false;
	return true;
}

static inline bool servo_bridge_suspend_dut(struct servo_bridge_node *n)
{
	if (servo_is_epoch_leader(n))
		return false;

	if (n->state == SERVO_STATE_ACTIVE) {
		if (n->active_proto != SERVO_PROTO_ADB &&
		    n->active_proto != SERVO_PROTO_FASTBOOT)
			return false;

		n->pre_suspend_proto = n->active_proto;
		n->active_proto = SERVO_PROTO_NONE;
		n->desired_proto = SERVO_PROTO_NONE;
		servo_bridge_trigger_reset(n, n->last_reset_tx_us);
		return true;
	}

	if (n->desired_proto != SERVO_PROTO_ADB &&
	    n->desired_proto != SERVO_PROTO_FASTBOOT)
		return false;

	n->pre_suspend_proto = n->desired_proto;
	n->desired_proto = SERVO_PROTO_NONE;
	return true;
}

static inline bool servo_bridge_wakeup_dut(struct servo_bridge_node *n,
					   uint32_t now_us)
{
	if (servo_is_epoch_leader(n) || n->state != SERVO_STATE_ACTIVE ||
	    n->pre_suspend_proto == SERVO_PROTO_NONE)
		return false;

	n->active_proto = n->pre_suspend_proto;
	n->desired_proto = n->active_proto;
	n->pre_suspend_proto = SERVO_PROTO_NONE;
	servo_bridge_trigger_reset(n, now_us);
	return true;
}

SERVO_HELPER bool servo_bridge_drain_usb_in(struct servo_bridge_node *n)
{
	bool emit_credit;

	if (n->state != SERVO_STATE_ACTIVE || !n->usb_in_busy)
		return false;

	emit_credit = n->credit_pending;
	n->usb_in_busy = false;
	n->credit_pending = false;
	n->pending_wait_ack = false;
	if (emit_credit) {
		n->pending_credit = true;
		n->pending_credit_epoch = n->epoch;
		n->pending_credit_seq = (n->rx_seq - 1U) & SERVO_SEQ_MASK;
	}
	return true;
}

#ifndef __AVR__
static inline uint16_t servo_adb_payload_chunks(const uint8_t *pkt)
{
	uint32_t pay_len = servo_read_u32_le(&pkt[12]);

	if (pay_len == 0U)
		return 0U;
	if (pay_len > SERVO_MAX_PAYLOAD_CLAMP)
		pay_len = SERVO_MAX_PAYLOAD_CLAMP;
	return (uint16_t)((pay_len + 63U) >> 6U);
}

static inline bool
servo_adb_session_host_drain_usb_in(struct servo_bridge_node *n)
{
	bool drained = servo_bridge_drain_usb_in(n);

	if (drained && n->active_proto == SERVO_PROTO_ADB &&
	    n->adb_cnxn_resp_rem_chunks > 0U) {
		n->adb_cnxn_resp_rem_chunks--;
	}
	return drained;
}

static inline bool servo_adb_session_on_host_out(struct servo_bridge_node *n,
						 const uint8_t *pkt,
						 uint8_t len,
						 bool *disarm_ep_in)
{
	if (disarm_ep_in != NULL)
		*disarm_ep_in = false;

	if (!servo_is_epoch_leader(n) || n->state != SERVO_STATE_ACTIVE ||
	    n->active_proto != SERVO_PROTO_ADB)
		return true;

	if (servo_is_adb_cnxn(pkt, len, n->active_proto)) {
		uint16_t pay_chunks = servo_adb_payload_chunks(pkt);

		if (n->adb_cnxn_resp_rem_chunks > 0U) {
			n->adb_cnxn_drop_out_chunks = pay_chunks;
			return false;
		}

		n->adb_awaiting_cnxn = false;
		n->adb_pending_syncs = 1U;
		n->adb_cnxn_req_rem_chunks = pay_chunks;
		n->adb_cnxn_resp_rem_chunks = (pay_chunks == 0U) ? 1U : 0U;
		n->adb_cnxn_drop_out_chunks = 0U;

		if (n->usb_in_busy) {
			(void)servo_bridge_drain_usb_in(n);
			if (disarm_ep_in != NULL)
				*disarm_ep_in = true;
		}
		return true;
	}

	if (n->adb_cnxn_drop_out_chunks > 0U) {
		n->adb_cnxn_drop_out_chunks--;
		return false;
	}

	if (n->adb_awaiting_cnxn)
		return false;

	if (n->adb_cnxn_req_rem_chunks > 0U) {
		n->adb_cnxn_req_rem_chunks--;
		if (n->adb_cnxn_req_rem_chunks == 0U)
			n->adb_cnxn_resp_rem_chunks = 1U;
	}
	return true;
}

static inline bool servo_adb_session_on_host_cnxn(struct servo_bridge_node *n)
{
	if (!servo_is_epoch_leader(n) || n->state != SERVO_STATE_ACTIVE ||
	    n->active_proto != SERVO_PROTO_ADB ||
	    n->adb_cnxn_resp_rem_chunks > 0U)
		return false;

	n->adb_awaiting_cnxn = false;
	n->adb_pending_syncs = 1U;
	n->adb_cnxn_req_rem_chunks = 0U;
	n->adb_cnxn_resp_rem_chunks = 1U;
	n->adb_cnxn_drop_out_chunks = 0U;

	if (n->usb_in_busy) {
		(void)servo_bridge_drain_usb_in(n);
		return true;
	}
	return false;
}

static inline bool
servo_adb_session_filter_rx_data(struct servo_bridge_node *n,
				 struct servo_bridge_rx_effects *fx)
{
	if (fx == NULL || !fx->usb_in_armed)
		return false;

	if (!servo_is_epoch_leader(n) || n->active_proto != SERVO_PROTO_ADB)
		return true;

	if (fx->usb_in_len == 0U) {
		n->adb_pending_syncs = 0U;
		fx->usb_in_armed = false;
		(void)servo_bridge_drain_usb_in(n);
		return false;
	}

	if (n->adb_awaiting_cnxn || n->adb_pending_syncs > 0U) {
		fx->usb_in_armed = false;
		(void)servo_bridge_drain_usb_in(n);
		return false;
	}

	if (servo_is_adb_cnxn(fx->usb_in_payload, fx->usb_in_len,
			      n->active_proto)) {
		uint16_t hs_pay_chunks =
			servo_adb_payload_chunks(fx->usb_in_payload);

		n->adb_cnxn_resp_rem_chunks = (uint16_t)(1U + hs_pay_chunks);
	}

	return true;
}
#endif /* !__AVR__ */

static inline bool servo_bridge_can_send_data(const struct servo_bridge_node *n)
{
	return n->state == SERVO_STATE_ACTIVE &&
	       (n->active_proto == SERVO_PROTO_ADB ||
		n->active_proto == SERVO_PROTO_FASTBOOT) &&
	       n->tx_credits == 1U && !n->has_tx_data;
}

static inline bool servo_bridge_send_data(struct servo_bridge_node *n,
					  const uint8_t *data, uint8_t len)
{
	uint8_t i;

	if (!servo_bridge_can_send_data(n) || len > SERVO_MAX_DATA_LEN)
		return false;

	n->tx_credits = 0U;
	n->has_tx_data = true;
	n->tx_data_seq = n->tx_seq & SERVO_SEQ_MASK;
	n->tx_data_len = len;
	for (i = 0U; i < len; ++i)
		n->tx_data_buf[i] = (data != NULL) ? data[i] : 0U;

#ifndef __AVR__
	servo_clamp_adb_cnxn(n->tx_data_buf, len, n->active_proto);
#endif
	n->pending_data_tx = true;
	return true;
}

static inline bool
servo_bridge_can_send_key_cmd(const struct servo_bridge_node *n)
{
	return servo_is_epoch_leader(n) && n->kbd_tx_credits > 0U &&
	       !n->pending_key_cmd;
}

static inline bool servo_bridge_send_key_cmd(struct servo_bridge_node *n,
					     uint8_t ch)
{
	if (!servo_is_epoch_leader(n))
		return false;

	n->pending_key_cmd = true;
	n->pending_key_cmd_ch = ch;
	return true;
}

static inline bool servo_bridge_send_key_ack(struct servo_bridge_node *n,
					     uint8_t ack_ch)
{
	n->pending_key_ack = true;
	n->pending_key_ack_ch = ack_ch;
	return true;
}

static inline void servo_bridge_on_corrupt_rx(struct servo_bridge_node *n)
{
	if (n->state == SERVO_STATE_ACTIVE) {
		n->pending_nak = true;
		n->pending_nak_epoch = n->epoch;
		n->pending_nak_seq = n->rx_seq & SERVO_SEQ_MASK;
	}
}

SERVO_HELPER void
servo_bridge_clear_rx_effects(const struct servo_bridge_node *n,
			      struct servo_bridge_rx_effects *fx)
{
	if (fx == NULL)
		return;
	fx->usb_in_armed = false;
	fx->usb_in_len = 0U;
	fx->key_cmd_received = false;
	fx->key_cmd_ch = 0U;
	fx->key_ack_received = false;
	fx->key_ack_ch = 0U;
	fx->proto_changed = false;
	fx->new_active_proto = n->active_proto;
	fx->epoch_history_cleared = false;
	fx->cleared_epoch = 0U;
	fx->reset_occurred = false;
	fx->peer_kick_received = false;
	fx->arm_reset_zlp = false;
}

static inline void
servo_bridge_on_rx_h2h_data(struct servo_bridge_node *n, uint8_t seq,
			    uint8_t arg_len, const uint8_t *payload,
			    struct servo_bridge_rx_effects *fx)
{
	uint8_t expected_seq = n->rx_seq & SERVO_SEQ_MASK;
	uint8_t prev_seq = (n->rx_seq - 1U) & SERVO_SEQ_MASK;
	uint8_t i;

	if (seq == expected_seq) {
		if (!n->usb_in_busy) {
			uint8_t copy_len = (arg_len <= SERVO_MAX_DATA_LEN) ?
						   arg_len :
						   SERVO_MAX_DATA_LEN;
			n->rx_seq = (n->rx_seq + 1U) & SERVO_SEQ_MASK;
			n->usb_in_busy = true;
			n->credit_pending = true;
			n->pending_wait_ack = true;
			n->pending_wait_seq = seq;
			if (fx != NULL) {
				fx->usb_in_armed = true;
				fx->usb_in_len = copy_len;
				for (i = 0U; i < copy_len; ++i) {
					fx->usb_in_payload[i] =
						(payload != NULL) ? payload[i] :
								    0U;
				}
#ifndef __AVR__
				servo_clamp_adb_cnxn(fx->usb_in_payload,
						     copy_len, n->active_proto);
#endif
			}
		} else {
			n->pending_wait_ack = true;
			n->pending_wait_seq = seq;
		}
	} else if (seq == prev_seq) {
		if (!n->usb_in_busy) {
			n->pending_credit = true;
			n->pending_credit_epoch = n->epoch;
			n->pending_credit_seq = seq;
			n->pending_wait_ack = false;
		} else {
			n->pending_wait_ack = true;
			n->pending_wait_seq = seq;
		}
	}
}

static inline void
servo_bridge_on_rx_reset_ack(struct servo_bridge_node *n, bool is_sys,
			     uint8_t seq, uint8_t frame_proto,
			     struct servo_bridge_rx_effects *fx)
{
	uint8_t eff_proto;

	if (n->state != SERVO_STATE_RESETTING || is_sys != n->booted ||
	    (!n->booted && seq != n->epoch))
		return;

	if (!servo_is_epoch_leader(n)) {
		bool adopt =
			(n->booted && n->active_proto == SERVO_PROTO_NONE &&
			 n->desired_proto == SERVO_PROTO_NONE);
		eff_proto = adopt ? frame_proto : n->active_proto;
		if (frame_proto != eff_proto)
			return;
		if (adopt)
			n->desired_proto = eff_proto;
		n->state = SERVO_STATE_ACTIVE;
		servo_bridge_apply_h2h_reset(n, seq, 1U, eff_proto, fx);
		n->booted = false;
		if (n->desired_proto != n->active_proto) {
			uint8_t saved = n->pre_suspend_proto;
			(void)servo_bridge_set_proto(n, n->desired_proto,
						     n->last_reset_tx_us);
			if (n->desired_proto == SERVO_PROTO_NONE)
				n->pre_suspend_proto = saved;
		}
	} else {
		eff_proto = (is_sys && frame_proto == SERVO_PROTO_NONE) ?
				    n->active_proto :
				    frame_proto;
		n->state = SERVO_STATE_ACTIVE;
		n->desired_proto = eff_proto;
		servo_bridge_apply_h2h_reset(n, seq, 1U, eff_proto, fx);
		n->booted = false;
	}
}

static inline void servo_bridge_on_rx_frame(struct servo_bridge_node *n,
					    uint8_t type, uint8_t arg_len,
					    const uint8_t *payload,
					    struct servo_bridge_rx_effects *fx)
{
	uint8_t ch = SERVO_GET_CHAN(type);
	uint8_t op = SERVO_GET_OP(type);
	uint8_t seq = SERVO_GET_SEQ(type);

	servo_bridge_clear_rx_effects(n, fx);

	if (ch == SERVO_CHAN_KBD) {
		if (op == SERVO_OP_DATA && !servo_is_epoch_leader(n)) {
			n->pending_key_ack = true;
			n->pending_key_ack_seq = seq;
			n->pending_key_ack_ch = (uint8_t)(~arg_len & 0xFFU);
			if (fx != NULL) {
				fx->key_cmd_received = true;
				fx->key_cmd_ch = arg_len;
			}
		} else if (op == SERVO_OP_CREDIT && servo_is_epoch_leader(n)) {
			n->kbd_tx_credits = 1U;
			n->kbd_tx_seq = (n->kbd_tx_seq + 1U) & SERVO_SEQ_MASK;
			if (fx != NULL) {
				fx->key_ack_received = true;
				fx->key_ack_ch = arg_len;
			}
		}
		return;
	}

	if (ch == SERVO_CHAN_H2H && op <= SERVO_OP_NAK) {
		if (n->state != SERVO_STATE_ACTIVE)
			return;
		if (op == SERVO_OP_DATA) {
			servo_bridge_on_rx_h2h_data(n, seq, arg_len, payload,
						    fx);
		} else if ((arg_len & SERVO_SEQ_MASK) == n->epoch &&
			   n->tx_credits == 0U && n->has_tx_data &&
			   seq == (n->tx_data_seq & SERVO_SEQ_MASK)) {
			if (op == SERVO_OP_CREDIT) {
				n->tx_credits = 1U;
				n->tx_seq = (n->tx_seq + 1U) & SERVO_SEQ_MASK;
				n->tx_link_acked = false;
				servo_bridge_clear_tx_buf(n);
			} else if (op == SERVO_OP_WAIT) {
				n->tx_link_acked = true;
			} else {
				n->pending_data_tx = true;
			}
		}
		return;
	}

	if (ch == SERVO_CHAN_SYS || ch == SERVO_CHAN_H2H) {
		bool is_sys = (ch == SERVO_CHAN_SYS);
		uint8_t old_proto = n->active_proto;
		uint8_t frame_proto = arg_len & SERVO_RESET_PROTO_MASK;

		if (op == SERVO_OP_RESET_REQ) {
			uint8_t new_proto;
			if ((arg_len & SERVO_RESET_FLAG_PEER_KICK) != 0U &&
			    fx != NULL)
				fx->peer_kick_received = true;

			if (servo_is_epoch_leader(n)) {
				new_proto = (is_sys &&
					     frame_proto == SERVO_PROTO_NONE) ?
						    n->active_proto :
						    frame_proto;
			} else {
				new_proto =
					(n->booted &&
					 n->active_proto == SERVO_PROTO_NONE &&
					 n->desired_proto == SERVO_PROTO_NONE) ?
						frame_proto :
						n->active_proto;
			}

			if (is_sys) {
				if (n->booted) {
					n->active_proto = new_proto;
					if (!servo_is_epoch_leader(n) &&
					    n->desired_proto ==
						    SERVO_PROTO_NONE) {
						n->desired_proto = new_proto;
					}
					n->pending_reset_ack = true;
					n->pending_reset_ack_epoch = 0U;
					n->pending_reset_ack_boot = true;
					n->pending_reset_ack_proto = new_proto;
					if (frame_proto != old_proto) {
						n->pending_reset_req = true;
						n->last_reset_tx_us =
							n->last_tx_us;
					}
				} else {
					uint8_t next_epoch =
						(n->state ==
						 SERVO_STATE_ACTIVE) ?
							SERVO_NEXT_EPOCH(
								n->epoch) :
							n->epoch;
					n->state = SERVO_STATE_RESETTING;
					servo_bridge_apply_h2h_reset(
						n, next_epoch, 0U, new_proto,
						fx);
					n->pending_reset_ack = true;
					n->pending_reset_ack_epoch = next_epoch;
					n->pending_reset_ack_boot = true;
					n->pending_reset_ack_proto = new_proto;
					n->pending_reset_req = true;
					n->last_reset_tx_us = n->last_tx_us;
				}
			} else if (!n->booted &&
				   (seq == n->epoch ||
				    SERVO_IS_NEWER_EPOCH(seq, n->epoch))) {
				if (!(n->state == SERVO_STATE_ACTIVE &&
				      seq == n->epoch &&
				      new_proto == n->active_proto)) {
					uint8_t cred = (n->state ==
							SERVO_STATE_ACTIVE) ?
							       1U :
							       0U;
					servo_bridge_apply_h2h_reset(
						n, seq, cred, new_proto, fx);
					if (n->state != SERVO_STATE_ACTIVE &&
					    !servo_is_epoch_leader(n) &&
					    frame_proto != old_proto) {
						n->pending_reset_req = true;
						n->last_reset_tx_us =
							n->last_tx_us;
					}
				}
				n->pending_reset_ack = true;
				n->pending_reset_ack_epoch = seq;
				n->pending_reset_ack_boot = false;
				n->pending_reset_ack_proto = new_proto;
			}
		} else if (op == SERVO_OP_RESET_ACK) {
			servo_bridge_on_rx_reset_ack(n, is_sys, seq,
						     frame_proto, fx);
		}

		if (fx != NULL && n->active_proto != old_proto) {
			fx->proto_changed = true;
			fx->new_active_proto = n->active_proto;
		}
	}
}

static inline void servo_bridge_on_rx_event(struct servo_bridge_node *n,
					    enum servo_rx_event evt,
					    struct servo_bridge_rx_effects *fx)
{
	if (n->rx.timeout_occurred) {
		n->rx.timeout_occurred = false;
		servo_bridge_on_corrupt_rx(n);
	}

	switch (evt) {
	case SERVO_RX_EVT_FRAME_COMPLETE:
		servo_bridge_on_rx_frame(n, n->rx.type, n->rx.arg_len,
					 n->rx.payload, fx);
		return;
	case SERVO_RX_EVT_CRC_ERROR:
	case SERVO_RX_EVT_TIMEOUT_ERROR:
	case SERVO_RX_EVT_INVALID_HEADER:
		servo_bridge_on_corrupt_rx(n);
		break;
	case SERVO_RX_EVT_NONE:
	default:
		break;
	}
	servo_bridge_clear_rx_effects(n, fx);
}

static inline bool
servo_bridge_on_retransmit_timeout(struct servo_bridge_node *n)
{
	if (n->state == SERVO_STATE_RESETTING) {
		n->pending_reset_req = true;
		return true;
	}
	if (n->state == SERVO_STATE_ACTIVE && n->tx_credits == 0U &&
	    n->has_tx_data) {
		n->pending_data_tx = true;
		return true;
	}
	return false;
}

static inline void servo_bridge_check_timers(struct servo_bridge_node *n,
					     uint32_t now_us)
{
	if (servo_frame_rx_check_timeout(&n->rx, now_us) ==
	    SERVO_RX_EVT_TIMEOUT_ERROR) {
		n->rx.timeout_occurred = false;
		servo_bridge_on_corrupt_rx(n);
	}
	if (n->state == SERVO_STATE_RESETTING && !n->pending_reset_req &&
	    (uint32_t)(now_us - n->last_reset_tx_us) >=
		    SERVO_RETRANSMIT_TIMEOUT_US) {
		(void)servo_bridge_on_retransmit_timeout(n);
	}
	if (n->state == SERVO_STATE_ACTIVE && n->tx_credits == 0U &&
	    n->has_tx_data && !n->pending_data_tx) {
		uint32_t timeout_us = n->tx_link_acked ?
					      SERVO_WAIT_KEEPALIVE_TIMEOUT_US :
					      SERVO_RETRANSMIT_TIMEOUT_US;
		if ((uint32_t)(now_us - n->last_tx_us) >= timeout_us) {
			(void)servo_bridge_on_retransmit_timeout(n);
		}
	}
	if (n->kbd_tx_credits == 0U && (uint32_t)(now_us - n->last_kbd_tx_us) >=
					       SERVO_KBD_CREDIT_TIMEOUT_US) {
		n->kbd_tx_credits = 1U;
		n->kbd_tx_seq = (n->kbd_tx_seq + 1U) & SERVO_SEQ_MASK;
	}
}

static inline size_t
servo_bridge_pop_tx_frame(struct servo_bridge_node *n, size_t tx_ring_free,
			  uint8_t out_frame[SERVO_MAX_FRAME_BYTES],
			  uint32_t now_us)
{
	uint8_t type = 0U;
	uint8_t arg = 0U;
	const uint8_t *payload = NULL;
	bool has_p1 = true;

	if (n->pending_reset_ack) {
		if (tx_ring_free < 4U)
			return 0U;
		type = SERVO_MAKE_TYPE(n->pending_reset_ack_boot ?
					       SERVO_CHAN_SYS :
					       SERVO_CHAN_H2H,
				       SERVO_OP_RESET_ACK,
				       n->pending_reset_ack_epoch);
		arg = (uint8_t)((n->local_host_active ?
					 SERVO_RESET_FLAG_HOST_ACTIVE :
					 0x00U) |
				(n->pending_reset_ack_proto &
				 SERVO_RESET_PROTO_MASK));
		n->pending_reset_ack = false;
	} else if (n->pending_reset_req) {
		if (tx_ring_free < 4U)
			return 0U;
		type = SERVO_MAKE_TYPE(n->booted ? SERVO_CHAN_SYS :
						   SERVO_CHAN_H2H,
				       SERVO_OP_RESET_REQ, n->epoch);
		arg = (uint8_t)((n->local_host_active ?
					 SERVO_RESET_FLAG_HOST_ACTIVE :
					 0x00U) |
				(n->send_peer_kick ?
					 SERVO_RESET_FLAG_PEER_KICK :
					 0x00U) |
				(n->active_proto & SERVO_RESET_PROTO_MASK));
		n->pending_reset_req = false;
		n->send_peer_kick = false;
		n->last_tx_us = now_us;
		n->last_reset_tx_us = now_us;

	} else if (n->pending_credit) {
		if (tx_ring_free < 4U)
			return 0U;
		type = SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_CREDIT,
				       n->pending_credit_seq);
		arg = n->pending_credit_epoch & SERVO_EPOCH_MASK;
		n->pending_credit = false;
		n->pending_wait_ack = false;
	} else if (n->pending_wait_ack) {
		if (tx_ring_free < 4U)
			return 0U;
		type = SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_WAIT,
				       n->pending_wait_seq);
		arg = n->epoch & SERVO_EPOCH_MASK;
		n->pending_wait_ack = false;
	} else if (n->pending_nak) {
		if (tx_ring_free < 4U)
			return 0U;
		type = SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_NAK,
				       n->pending_nak_seq);
		arg = n->pending_nak_epoch & SERVO_EPOCH_MASK;
		n->pending_nak = false;

	} else if (n->pending_key_ack) {
		if (tx_ring_free < 4U)
			return 0U;
		type = SERVO_MAKE_TYPE(SERVO_CHAN_KBD, SERVO_OP_CREDIT,
				       n->pending_key_ack_seq);
		arg = n->pending_key_ack_ch;
		n->pending_key_ack = false;
	} else if (n->pending_key_cmd) {
		if (tx_ring_free < SERVO_KEY_TX_MIN_FREE)
			return 0U;
		type = SERVO_MAKE_TYPE(SERVO_CHAN_KBD, SERVO_OP_DATA,
				       n->kbd_tx_seq);
		arg = n->pending_key_cmd_ch;
		n->kbd_tx_credits = 0U;
		n->last_kbd_tx_us = now_us;
		n->pending_key_cmd = false;
	} else {
		has_p1 = false;
	}

	if (!has_p1) {
		/* Priority 3: Bulk Data */
		if (n->state == SERVO_STATE_ACTIVE && n->pending_data_tx &&
		    n->has_tx_data &&
		    tx_ring_free >= SERVO_DATA_TX_MIN_FREE(n->tx_data_len)) {
			type = SERVO_MAKE_TYPE(SERVO_CHAN_H2H, SERVO_OP_DATA,
					       n->tx_data_seq);
			arg = n->tx_data_len;
			n->pending_data_tx = false;
			n->last_tx_us = now_us;
			payload = n->tx_data_buf;
		} else {
			return 0U;
		}
	}

	return servo_frame_encode(type, arg, payload, out_frame);
}

#define servo_bridge_poll_tx servo_bridge_pop_tx_frame

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_SERVO_USB_BRIDGE_FRAME_H */
