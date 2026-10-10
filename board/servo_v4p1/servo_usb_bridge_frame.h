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

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. Wire Frame Constants, Opcodes, & Protocol Identifiers
 * ========================================================================= */

#define SERVO_BRIDGE_SYNC 0xA5U

/* Frame Opcodes (high nibble [7:4] of TYPE byte; bit [3] is reserved 0) */
#define SERVO_FRAME_DATA 0x10U /* | (seq << 2) | (epoch & 0x03) */
#define SERVO_FRAME_CREDIT 0x20U /* | (epoch & 0x03); arg = ack_seq */
#define SERVO_FRAME_NAK 0x30U /* | (epoch & 0x03); arg = expected_seq */
#define SERVO_FRAME_RESET_REQ \
	0x40U /* | (epoch & 0x03); arg = (boot ? 0x80 : 0) | proto */
#define SERVO_FRAME_RESET_ACK \
	0x50U /* | (epoch & 0x03); arg = (boot ? 0x80 : 0) | proto */
#define SERVO_FRAME_KEY_CMD 0x60U /* arg = keycode ch */
#define SERVO_FRAME_KEY_ACK 0x70U /* arg = ~keycode & 0xFF */

#define SERVO_OPCODE_MASK 0xF0U
#define SERVO_TYPE_RESERVED_BIT 0x08U
#define SERVO_CTRL_SEQ_MASK 0x0CU
#define SERVO_EPOCH_MASK 0x03U
#define SERVO_DATA_SEQ_SHIFT 2U

/* 2-bit modulo-4 directional epoch arithmetic */
#define SERVO_NEXT_EPOCH(e) (((uint8_t)(e) + 1U) & SERVO_EPOCH_MASK)
#define SERVO_IS_NEWER_EPOCH(r, c) \
	((((uint8_t)(r) - (uint8_t)(c)) & SERVO_EPOCH_MASK) == 1U)

/* RESET_REQ / RESET_ACK ARG byte encoding */
#define SERVO_RESET_FLAG_BOOT 0x80U
#define SERVO_RESET_PROTO_MASK 0x03U

/* Bridged USB Protocols (ATmega32U4 is Protocol Authority) */
#define SERVO_PROTO_NONE 0x00U
#define SERVO_PROTO_ADB 0x01U
#define SERVO_PROTO_FASTBOOT 0x03U

/* Packet & Ring-Buffer Sizing Constants */
#define SERVO_MAX_DATA_LEN 64U
#define SERVO_MAX_FRAME_BYTES 68U
#define SERVO_TX_RING_BYTES 128U
#define SERVO_RX_RING_BYTES 256U

/*
 * 16B (4 x 4B) reserved in 128B UART TX buffer for all 4 Priority 1 control
 * types
 */
#define SERVO_CTRL_TX_HEADROOM 16U
#define SERVO_KEY_TX_MIN_FREE (4U + SERVO_CTRL_TX_HEADROOM)
#define SERVO_DATA_TX_MIN_FREE(len) \
	((size_t)(4U + (size_t)(len) + SERVO_CTRL_TX_HEADROOM))

/* In-flight ADB A_CNXN max_payload clamp (16 KiB = ~200 ms at 1.000 Mbps) */
#define SERVO_MAX_PAYLOAD_CLAMP 16384U

/* Protocol Timers (microseconds) */
#define SERVO_RETRANSMIT_TIMEOUT_US \
	15000U /* 15 ms T_RET retransmission timer */
#define SERVO_RX_FRAME_TIMEOUT_US \
	10000U /* 10 ms inter-byte gap T_RX_FRAME deadline */
#define SERVO_ADB_TX_STALE_TIMEOUT_US \
	150000U /* 150 ms stale EP7 IN drain timeout (S7) */

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
	return (frame_seq & 0x01U) != (rx_seq & 0x01U);
}

/* =========================================================================
 * 2. CRC-8-CCITT (poly = 0x07, init = 0x00, MSB-first, no final XOR)
 * ========================================================================= */

static inline uint8_t servo_crc8_ccitt(uint8_t crc, uint8_t data)
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

	servo_frame_encode_header(type, arg_len, out_frame, &crc);
	if ((type & SERVO_OPCODE_MASK) == SERVO_FRAME_DATA) {
		uint8_t len = (arg_len <= SERVO_MAX_DATA_LEN) ?
				      arg_len :
				      SERVO_MAX_DATA_LEN;
		out_frame[2] = len;
		crc = servo_crc8_ccitt(0x00U, type);
		crc = servo_crc8_ccitt(crc, len);
		for (i = 0U; i < len; ++i) {
			uint8_t b = (payload != NULL) ? payload[i] : 0U;
			out_frame[3U + i] = b;
			crc = servo_crc8_ccitt(crc, b);
		}
		out_frame[3U + len] = crc;
		return (size_t)(4U + len);
	}

	out_frame[3] = crc;
	return 4U;
}

/* =========================================================================
 * 3. Byte-Wise Little-Endian A_CNXN Detection & max_payload Clamping
 * ========================================================================= */

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

static inline bool servo_is_adb_cnxn(const uint8_t *pkt, uint8_t len,
				     uint8_t active_proto)
{
	if (pkt == NULL || active_proto != SERVO_PROTO_ADB ||
	    len != SERVO_ADB_MSG_HEADER_LEN)
		return false;

	return servo_read_u32_le(&pkt[0]) == SERVO_ADB_CNXN_CMD &&
	       servo_read_u32_le(&pkt[20]) == SERVO_ADB_CNXN_MAGIC;
}

static inline void servo_clamp_adb_cnxn(uint8_t *pkt, uint8_t len,
					uint8_t active_proto)
{
	uint32_t arg1;

	if (!servo_is_adb_cnxn(pkt, len, active_proto))
		return;

	arg1 = servo_read_u32_le(&pkt[8]);
	if (arg1 > SERVO_MAX_PAYLOAD_CLAMP)
		servo_write_u32_le(&pkt[8], SERVO_MAX_PAYLOAD_CLAMP);
}

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
	SERVO_RX_EVT_CRC_ERROR = 2, /* Triggers FRAME_NAK */
	SERVO_RX_EVT_TIMEOUT_ERROR = 3, /* T_RX_FRAME expired; triggers
					   FRAME_NAK */
	SERVO_RX_EVT_INVALID_HEADER = 4, /* Bad opcode or len > 64; triggers NAK
					  */
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
	uint8_t i;

	p->state = SERVO_RX_WAIT_SYNC;
	p->type = 0U;
	p->arg_len = 0U;
	for (i = 0U; i < SERVO_MAX_DATA_LEN; ++i)
		p->payload[i] = 0U;
	p->bytes_read = 0U;
	p->running_crc = 0U;
	p->frame_start_us = 0U;
	p->timeout_occurred = false;
}

static inline bool servo_frame_is_valid_type(uint8_t type)
{
	uint8_t op = type & SERVO_OPCODE_MASK;

	if ((type & SERVO_TYPE_RESERVED_BIT) != 0U)
		return false;

	switch (op) {
	case SERVO_FRAME_DATA:
		return true;
	case SERVO_FRAME_CREDIT:
	case SERVO_FRAME_NAK:
	case SERVO_FRAME_RESET_REQ:
	case SERVO_FRAME_RESET_ACK:
		return (type & SERVO_CTRL_SEQ_MASK) == 0U;
	case SERVO_FRAME_KEY_CMD:
	case SERVO_FRAME_KEY_ACK:
		return (type & 0x0FU) == 0U;
	default:
		return false;
	}
}

static inline bool servo_frame_is_valid_arg(uint8_t type, uint8_t arg_len)
{
	uint8_t op = type & SERVO_OPCODE_MASK;

	switch (op) {
	case SERVO_FRAME_DATA:
		return arg_len <= SERVO_MAX_DATA_LEN;
	case SERVO_FRAME_CREDIT:
	case SERVO_FRAME_NAK:
		return arg_len <= 1U;
	case SERVO_FRAME_RESET_REQ:
	case SERVO_FRAME_RESET_ACK:
		if ((arg_len & (uint8_t)~(SERVO_RESET_FLAG_BOOT |
					  SERVO_RESET_PROTO_MASK)) != 0U)
			return false;
		return servo_is_valid_proto(arg_len & SERVO_RESET_PROTO_MASK);
	case SERVO_FRAME_KEY_CMD:
	case SERVO_FRAME_KEY_ACK:
		return true;
	default:
		return false;
	}
}

static inline bool servo_frame_is_valid_header(uint8_t type, uint8_t arg_len)
{
	return servo_frame_is_valid_type(type) &&
	       servo_frame_is_valid_arg(type, arg_len);
}

static inline enum servo_rx_event
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

	/*
	 * Whole-frame deadline check (T_RX_FRAME = 2000 us from
	 * frame_start_us). If a partial frame timed out and the newly arrived
	 * byte is 0xA5 (SYNC), record timeout_occurred and immediately consume
	 * 0xA5 as the start of the new frame rather than swallowing the first
	 * byte of a retransmission.
	 */
	if (p->state != SERVO_RX_WAIT_SYNC &&
	    (uint32_t)(now_us - p->frame_start_us) >=
		    SERVO_RX_FRAME_TIMEOUT_US) {
		p->state = SERVO_RX_WAIT_SYNC;
		p->bytes_read = 0U;
		p->running_crc = 0U;
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
		/*
		 * 0xA5 is never a valid TYPE byte (high nibble 0xA0). If a
		 * duplicate SYNC byte arrives in WAIT_TYPE, re-anchor
		 * frame_start_us without dropping out of WAIT_TYPE.
		 */
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
		if ((p->type & SERVO_OPCODE_MASK) == SERVO_FRAME_DATA &&
		    byte > 0U) {
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
	/* Host C-to-TLA+ refinement oracle hooks (no-op in STM32/ATmega
	 * firmware) */
	bool epoch_history_cleared;
	uint8_t cleared_epoch;
};

struct servo_bridge_node {
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

	/*
	 * ADB session resynchronization & stale pipeline drain state
	 * (adb_pending_syncs and adb_stale_drain in ServoAdbSession.tla).
	 * Used by STM32 (is_epoch_leader == true) in SERVO_PROTO_ADB.
	 */
	uint8_t adb_pending_syncs;
	bool adb_stale_drain;

	/* Outbound DATA retransmission buffer (tx_buf in ServoUsbBridge.tla) */
	bool has_tx_data;
	uint8_t tx_data_epoch;
	uint8_t tx_data_seq;
	uint8_t tx_data_len;
	uint8_t tx_data_buf[SERVO_MAX_DATA_LEN];

	/* Coalesced Priority 1 / Priority 2 / Priority 3 TX pending flags */
	bool pending_reset_req;

	bool pending_reset_ack;
	uint8_t pending_reset_ack_epoch;
	bool pending_reset_ack_boot;
	uint8_t pending_reset_ack_proto;

	bool pending_credit;
	uint8_t pending_credit_epoch;
	uint8_t pending_credit_seq;

	bool pending_nak;
	uint8_t pending_nak_epoch;
	uint8_t pending_nak_seq;

	bool pending_key_cmd;
	uint8_t pending_key_cmd_ch;

	bool pending_key_ack;
	uint8_t pending_key_ack_ch;

	bool pending_data_tx;

	/* Microsecond retransmission timestamps & byte-stream RX parser */
	uint32_t last_tx_us;
	uint32_t last_reset_tx_us;
	struct servo_rx_parser rx;
};

static inline void servo_bridge_clear_tx_buf(struct servo_bridge_node *n)
{
	uint8_t i;

	n->has_tx_data = false;
	n->tx_data_epoch = 0U;
	n->tx_data_seq = 0U;
	n->tx_data_len = 0U;
	for (i = 0U; i < SERVO_MAX_DATA_LEN; ++i)
		n->tx_data_buf[i] = 0U;
	n->pending_data_tx = false;
}

/*
 * TLA+ Actions: Init / RebootNode(n)
 * Wipes local node RAM and RX parser state and schedules boot
 * RESET_REQ(0, 0, boot=true).
 */
static inline void servo_bridge_init(struct servo_bridge_node *n,
				     bool is_epoch_leader, uint32_t now_us)
{
	n->is_epoch_leader = is_epoch_leader;
	n->state = SERVO_STATE_RESETTING;
	n->epoch = 0U;
	n->booted = true;
	n->active_proto = SERVO_PROTO_NONE;
	n->desired_proto = SERVO_PROTO_NONE;
	n->pre_suspend_proto = SERVO_PROTO_NONE;

	n->tx_seq = 0U;
	n->rx_seq = 0U;
	n->tx_credits = 0U;
	n->usb_in_busy = false;
	n->credit_pending = false;
	n->adb_pending_syncs = 0U;
	n->adb_stale_drain = false;

	servo_bridge_clear_tx_buf(n);

	n->pending_reset_req = true;
	n->pending_reset_ack = false;
	n->pending_reset_ack_epoch = 0U;
	n->pending_reset_ack_boot = false;
	n->pending_reset_ack_proto = 0U;

	n->pending_credit = false;
	n->pending_credit_epoch = 0U;
	n->pending_credit_seq = 0U;

	n->pending_nak = false;
	n->pending_nak_epoch = 0U;
	n->pending_nak_seq = 0U;

	n->pending_key_cmd = false;
	n->pending_key_cmd_ch = 0U;

	n->pending_key_ack = false;
	n->pending_key_ack_ch = 0U;

	n->last_tx_us = now_us;
	n->last_reset_tx_us = now_us;
	servo_frame_rx_init(&n->rx);
}

/*
 * TLA+ Action: TriggerReset(n)
 * Local USB reset event; advances epoch only if leaving STATE_ACTIVE, preserves
 * booted flag if already in STATE_RESETTING, flushes USB staging state, and
 * schedules FRAME_RESET_REQ(next_epoch, active_proto, next_boot).
 */
static inline void servo_bridge_trigger_reset(struct servo_bridge_node *n,
					      uint32_t now_us)
{
	uint8_t next_epoch = (n->state == SERVO_STATE_ACTIVE) ?
				     SERVO_NEXT_EPOCH(n->epoch) :
				     n->epoch;
	bool next_boot = (n->state == SERVO_STATE_ACTIVE) ? false : n->booted;

	n->state = SERVO_STATE_RESETTING;
	n->epoch = next_epoch;
	n->booted = next_boot;
	n->tx_credits = 0U;
	n->tx_seq = 0U;
	n->rx_seq = 0U;
	n->usb_in_busy = false;
	n->credit_pending = false;
	n->adb_pending_syncs = 0U;
	n->adb_stale_drain = false;
	servo_bridge_clear_tx_buf(n);

	n->pending_reset_req = true;
	n->last_reset_tx_us = now_us;
}

/*
 * TLA+ Action: SetProtocol(proto) (ATmega Protocol Authority only; One Protocol
 * Per Epoch) Requires n->state == SERVO_STATE_ACTIVE so every protocol change
 * unconditionally advances epoch to SERVO_NEXT_EPOCH(n->epoch) and prevents
 * same-epoch P1->P2->P1 ABA aliasing.
 */
static inline bool servo_bridge_set_proto(struct servo_bridge_node *n,
					  uint8_t proto, uint32_t now_us)
{
	if (n->is_epoch_leader || n->state != SERVO_STATE_ACTIVE ||
	    !servo_is_valid_proto(proto) ||
	    (n->active_proto == proto && proto == SERVO_PROTO_NONE))
		return false;

	n->pre_suspend_proto = SERVO_PROTO_NONE;
	n->active_proto = proto;
	n->desired_proto = proto;
	servo_bridge_trigger_reset(n, now_us);
	return true;
}

/*
 * ATmega Firmware Helper: servo_bridge_request_proto(n, proto, now_us)
 * If ATmega is in SERVO_STATE_ACTIVE, immediately initiates SetProtocol(proto).
 * If ATmega is currently in SERVO_STATE_RESETTING, latches n->desired_proto =
 * proto so the protocol change is applied in a fresh epoch as soon as
 * SERVO_FRAME_RESET_ACK transitions n->state to SERVO_STATE_ACTIVE.
 */
static inline bool servo_bridge_request_proto(struct servo_bridge_node *n,
					      uint8_t proto, uint32_t now_us)
{
	if (n->is_epoch_leader || !servo_is_valid_proto(proto))
		return false;

	n->pre_suspend_proto = SERVO_PROTO_NONE;
	n->desired_proto = proto;
	if (n->state == SERVO_STATE_ACTIVE &&
	    (n->active_proto != proto || proto != SERVO_PROTO_NONE))
		return servo_bridge_set_proto(n, proto, now_us);
	return true;
}

/*
 * TLA+ Action: SuspendDut (ATmega Protocol Authority only; One Protocol Per
 * Epoch) In SERVO_STATE_ACTIVE, saves pre_suspend_proto = active_proto and
 * initiates a new epoch reset to SERVO_PROTO_NONE. In SERVO_STATE_RESETTING,
 * saves pre_suspend_proto = desired_proto and latches desired_proto =
 * SERVO_PROTO_NONE so the transition to SERVO_PROTO_NONE occurs in the next
 * epoch after RESET_ACK without clobbering pre_suspend_proto.
 */
static inline bool servo_bridge_suspend_dut(struct servo_bridge_node *n)
{
	if (n->is_epoch_leader)
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

/*
 * TLA+ Action: WakeupDut (ATmega Protocol Authority only; One Protocol Per
 * Epoch)
 */
static inline bool servo_bridge_wakeup_dut(struct servo_bridge_node *n,
					   uint32_t now_us)
{
	if (n->is_epoch_leader || n->state != SERVO_STATE_ACTIVE ||
	    n->pre_suspend_proto == SERVO_PROTO_NONE)
		return false;

	n->active_proto = n->pre_suspend_proto;
	n->desired_proto = n->active_proto;
	n->pre_suspend_proto = SERVO_PROTO_NONE;
	servo_bridge_trigger_reset(n, now_us);
	return true;
}

/*
 * TLA+ Action: DrainUsbIn(n)
 * Attached USB Host drains armed Bulk IN endpoint, releasing deferred CREDIT.
 */
static inline bool servo_bridge_drain_usb_in(struct servo_bridge_node *n)
{
	bool emit_credit;

	if (n->state != SERVO_STATE_ACTIVE || !n->usb_in_busy)
		return false;

	emit_credit = n->credit_pending;
	n->usb_in_busy = false;
	n->credit_pending = false;
	if (emit_credit) {
		n->pending_credit = true;
		n->pending_credit_epoch = n->epoch;
		n->pending_credit_seq = (uint8_t)(1U - n->rx_seq);
	}
	return true;
}

/*
 * TLA+ Action: Stm32ForwardHostToAtmega (Host A_CNXN branch in
 * ServoAdbSession.tla)
 *
 * Called on the STM32 (is_epoch_leader == true) when EP7 OUT receives a 24-byte
 * A_CNXN header in SERVO_PROTO_ADB. Sets adb_pending_syncs = 1U (awaiting the
 * matching 0-byte SYNC_ZLP barrier from adb-usb-bridge), clears
 * adb_stale_drain, and if EP7 IN is currently armed with a stale packet
 * (usb_in_busy == true), drains usb_in_busy (releasing deferred CREDIT to the
 * ATmega) and returns true so the STM32 caller disarms EP7 IN (tx_count = 0,
 * EP_TX_NAK without touching EP_TX_DTOG).
 */
static inline bool servo_adb_session_on_host_cnxn(struct servo_bridge_node *n)
{
	if (!n->is_epoch_leader || n->state != SERVO_STATE_ACTIVE ||
	    n->active_proto != SERVO_PROTO_ADB)
		return false;

	n->adb_pending_syncs = 1U;
	n->adb_stale_drain = false;

	if (n->usb_in_busy) {
		(void)servo_bridge_drain_usb_in(n);
		return true;
	}
	return false;
}

/*
 * TLA+ Action: Stm32StaleDrainTimeout (ServoAdbSession.tla)
 *
 * Called on the STM32 when EP7 IN has been armed (usb_in_busy == true) and
 * undrained by the Host for >= SERVO_ADB_TX_STALE_TIMEOUT_US (150 ms) in
 * SERVO_PROTO_ADB (e.g. after Host `adb pull` SIGKILL + `adb kill-server`).
 * Enters adb_stale_drain mode, drains usb_in_busy (releasing CREDIT to the
 * ATmega so the DUT's EP4 OUT never stalls), and returns true so the STM32
 * caller disarms EP7 IN (tx_count = 0, EP_TX_NAK without touching EP_TX_DTOG).
 */
static inline bool
servo_adb_session_on_stale_timeout(struct servo_bridge_node *n)
{
	if (!n->is_epoch_leader || n->state != SERVO_STATE_ACTIVE ||
	    n->active_proto != SERVO_PROTO_ADB || !n->usb_in_busy)
		return false;

	n->adb_stale_drain = true;
	(void)servo_bridge_drain_usb_in(n);
	return true;
}

/*
 * TLA+ Action: AtmegaForwardToStm32 (STM32 RX filter in ServoAdbSession.tla)
 *
 * Called on the STM32 immediately after servo_bridge_on_rx_event(n, evt, fx)
 * when fx->usb_in_armed is true in SERVO_PROTO_ADB:
 *   1. If fx->usb_in_len == 0U (0-byte SYNC_ZLP barrier emitted by
 * adb-usb-bridge upon switching to the new generation's adbd socket):
 *        - Clears n->adb_pending_syncs = 0U, clears n->adb_stale_drain,
 *          clears fx->usb_in_armed = false, and calls
 * servo_bridge_drain_usb_in(n) to immediately return SERVO_FRAME_CREDIT to the
 * ATmega without exposing the 0-byte barrier on EP7 IN.
 *   2. Else if n->adb_pending_syncs > 0U || n->adb_stale_drain:
 *        - Discards the pre-barrier stale packet (fx->usb_in_armed = false) and
 *          calls servo_bridge_drain_usb_in(n) to immediately return CREDIT to
 *          the ATmega.
 *   3. Else:
 *        - Returns true so the caller arms EP7 IN with fx->usb_in_payload.
 */
static inline bool
servo_adb_session_filter_rx_data(struct servo_bridge_node *n,
				 struct servo_bridge_rx_effects *fx)
{
	if (fx == NULL || !fx->usb_in_armed)
		return false;

	if (!n->is_epoch_leader || n->active_proto != SERVO_PROTO_ADB)
		return true;

	if (fx->usb_in_len == 0U) {
		n->adb_pending_syncs = 0U;
		n->adb_stale_drain = false;
		fx->usb_in_armed = false;
		(void)servo_bridge_drain_usb_in(n);
		return false;
	}

	if (n->adb_pending_syncs > 0U || n->adb_stale_drain) {
		fx->usb_in_armed = false;
		(void)servo_bridge_drain_usb_in(n);
		return false;
	}

	return true;
}

/*
 * TLA+ Action: SendData(n, data)
 */
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
	n->tx_data_epoch = n->epoch;
	n->tx_data_seq = n->tx_seq;
	n->tx_data_len = len;
	for (i = 0U; i < len; ++i)
		n->tx_data_buf[i] = (data != NULL) ? data[i] : 0U;
	for (i = len; i < SERVO_MAX_DATA_LEN; ++i)
		n->tx_data_buf[i] = 0U;

	servo_clamp_adb_cnxn(n->tx_data_buf, len, n->active_proto);
	n->pending_data_tx = true;
	return true;
}

/*
 * TLA+ Action: SendKeyCmd (STM32 Epoch Leader only)
 */
static inline bool
servo_bridge_can_send_key_cmd(const struct servo_bridge_node *n,
			      bool kbd_in_flight)
{
	return n->is_epoch_leader && !kbd_in_flight && !n->pending_key_cmd;
}

static inline bool servo_bridge_send_key_cmd(struct servo_bridge_node *n,
					     uint8_t ch)
{
	if (!n->is_epoch_leader)
		return false;

	n->pending_key_cmd = true;
	n->pending_key_cmd_ch = ch;
	return true;
}

/*
 * TLA+ Action: RecvCorrupt(n)
 * Schedules FRAME_NAK(epoch, rx_seq) when in STATE_ACTIVE.
 */
static inline void servo_bridge_on_corrupt_rx(struct servo_bridge_node *n)
{
	if (n->state == SERVO_STATE_ACTIVE) {
		n->pending_nak = true;
		n->pending_nak_epoch = n->epoch;
		n->pending_nak_seq = n->rx_seq;
	}
}

/*
 * TLA+ Actions: RecvData, RecvCredit, RecvNak, RecvResetReq, RecvResetAck,
 *               RecvKeyCmd, RecvKeyAck
 */
static inline void servo_bridge_on_rx_frame(struct servo_bridge_node *n,
					    uint8_t type, uint8_t arg_len,
					    const uint8_t *payload,
					    struct servo_bridge_rx_effects *fx)
{
	uint8_t op = type & SERVO_OPCODE_MASK;
	uint8_t frame_epoch = type & SERVO_EPOCH_MASK;
	uint8_t i;

	if (fx != NULL) {
		fx->usb_in_armed = false;
		fx->usb_in_len = 0U;
		for (i = 0U; i < SERVO_MAX_DATA_LEN; ++i)
			fx->usb_in_payload[i] = 0U;
		fx->key_cmd_received = false;
		fx->key_cmd_ch = 0U;
		fx->key_ack_received = false;
		fx->key_ack_ch = 0U;
		fx->proto_changed = false;
		fx->new_active_proto = n->active_proto;
		fx->epoch_history_cleared = false;
		fx->cleared_epoch = 0U;
	}

	switch (op) {
	case SERVO_FRAME_DATA: {
		uint8_t frame_seq = (type >> SERVO_DATA_SEQ_SHIFT) & 0x01U;

		if (n->state == SERVO_STATE_ACTIVE && frame_epoch == n->epoch) {
			if (!servo_is_duplicate_data(frame_seq, n->rx_seq)) {
				/* Case A: Fresh packet (frame_seq == rx_seq) */
				if (!n->usb_in_busy) {
					uint8_t copy_len =
						(arg_len <=
						 SERVO_MAX_DATA_LEN) ?
							arg_len :
							SERVO_MAX_DATA_LEN;
					n->rx_seq = (uint8_t)(1U - n->rx_seq);
					n->usb_in_busy = true;
					n->credit_pending = true;
					if (fx != NULL) {
						fx->usb_in_armed = true;
						fx->usb_in_len = copy_len;
						for (i = 0U; i < copy_len;
						     ++i) {
							fx->usb_in_payload[i] =
								(payload !=
								 NULL) ?
									payload[i] :
									0U;
						}
						servo_clamp_adb_cnxn(
							fx->usb_in_payload,
							copy_len,
							n->active_proto);
					}
				}
				/* If usb_in_busy: drop without toggling rx_seq
				 * (Case A2) */
			} else {
				/* Case B: Duplicate packet (frame_seq !=
				 * rx_seq) */
				if (!n->usb_in_busy) {
					n->pending_credit = true;
					n->pending_credit_epoch = n->epoch;
					n->pending_credit_seq = frame_seq;
				}
				/* If usb_in_busy: suppress duplicate credit
				 * until DrainUsbIn */
			}
		}
		break;
	}

	case SERVO_FRAME_CREDIT: {
		uint8_t ack_seq = arg_len & 0x01U;

		if (n->state == SERVO_STATE_ACTIVE && frame_epoch == n->epoch) {
			if (n->tx_credits == 0U && n->has_tx_data &&
			    n->tx_data_seq == ack_seq) {
				n->tx_credits = 1U;
				n->tx_seq = (uint8_t)(1U - n->tx_seq);
				servo_bridge_clear_tx_buf(n);
			}
		}
		break;
	}

	case SERVO_FRAME_NAK: {
		uint8_t expected_seq = arg_len & 0x01U;

		if (n->state == SERVO_STATE_ACTIVE && frame_epoch == n->epoch) {
			if (n->tx_credits == 0U && n->has_tx_data &&
			    n->tx_data_seq == expected_seq) {
				n->pending_data_tx = true;
			}
		}
		break;
	}

	case SERVO_FRAME_RESET_REQ: {
		uint8_t req_epoch = frame_epoch;
		uint8_t req_proto = arg_len & SERVO_RESET_PROTO_MASK;
		bool req_boot = (arg_len & SERVO_RESET_FLAG_BOOT) != 0U;
		uint8_t old_proto = n->active_proto;
		uint8_t new_proto = n->is_epoch_leader ? req_proto :
							 n->active_proto;

		if (req_boot) {
			if (n->booted) {
				if (n->is_epoch_leader ||
				    req_proto == n->active_proto) {
					n->active_proto = new_proto;
					n->pending_reset_ack = true;
					n->pending_reset_ack_epoch = 0U;
					n->pending_reset_ack_boot = true;
					n->pending_reset_ack_proto = new_proto;
				} else {
					n->pending_reset_req = true;
				}
			} else {
				uint8_t next_epoch =
					(n->state == SERVO_STATE_ACTIVE) ?
						SERVO_NEXT_EPOCH(n->epoch) :
						n->epoch;
				n->state = SERVO_STATE_RESETTING;
				n->epoch = next_epoch;
				n->tx_credits = 0U;
				n->tx_seq = 0U;
				n->rx_seq = 0U;
				n->usb_in_busy = false;
				n->credit_pending = false;
				n->adb_pending_syncs = 0U;
				n->adb_stale_drain = false;
				servo_bridge_clear_tx_buf(n);
				n->active_proto = new_proto;
				n->pending_reset_ack = true;
				n->pending_reset_ack_epoch = next_epoch;
				n->pending_reset_ack_boot = true;
				n->pending_reset_ack_proto = new_proto;
				if (fx != NULL) {
					fx->epoch_history_cleared = true;
					fx->cleared_epoch = next_epoch;
				}
			}
		} else {
			/* Runtime reset handshake (req_boot == false) */
			if (n->booted) {
				/* Boot barrier: ignore pre-boot runtime
				 * RESET_REQ */
			} else if (n->state == SERVO_STATE_ACTIVE) {
				if (req_epoch == n->epoch) {
					if (new_proto == n->active_proto) {
						/* Pure duplicate RESET_REQ:
						 * re-ack only */
						n->pending_reset_ack = true;
						n->pending_reset_ack_epoch =
							req_epoch;
						n->pending_reset_ack_boot =
							false;
						n->pending_reset_ack_proto =
							new_proto;
					} else {
						/* Protocol changed at same
						 * epoch */
						n->tx_credits = 1U;
						n->tx_seq = 0U;
						n->rx_seq = 0U;
						n->usb_in_busy = false;
						n->credit_pending = false;
						n->adb_pending_syncs = 0U;
						n->adb_stale_drain = false;
						servo_bridge_clear_tx_buf(n);
						n->active_proto = new_proto;
						n->pending_reset_ack = true;
						n->pending_reset_ack_epoch =
							req_epoch;
						n->pending_reset_ack_boot =
							false;
						n->pending_reset_ack_proto =
							new_proto;
						if (fx != NULL) {
							fx->epoch_history_cleared =
								true;
							fx->cleared_epoch =
								req_epoch;
						}
					}
				} else if (SERVO_IS_NEWER_EPOCH(req_epoch,
								n->epoch)) {
					/* Newer peer epoch */
					n->epoch = req_epoch;
					n->tx_credits = 1U;
					n->tx_seq = 0U;
					n->rx_seq = 0U;
					n->usb_in_busy = false;
					n->credit_pending = false;
					n->adb_pending_syncs = 0U;
					n->adb_stale_drain = false;
					servo_bridge_clear_tx_buf(n);
					n->active_proto = new_proto;
					n->pending_reset_ack = true;
					n->pending_reset_ack_epoch = req_epoch;
					n->pending_reset_ack_boot = false;
					n->pending_reset_ack_proto = new_proto;
					if (fx != NULL) {
						fx->epoch_history_cleared =
							true;
						fx->cleared_epoch = req_epoch;
					}
				}
				/* Stale epoch: discard without rolling epoch
				 * backwards */
			} else {
				/* n->state == SERVO_STATE_RESETTING &&
				 * !n->booted */
				if (req_epoch == n->epoch ||
				    SERVO_IS_NEWER_EPOCH(req_epoch, n->epoch)) {
					if (n->is_epoch_leader ||
					    req_proto == n->active_proto) {
						n->epoch = req_epoch;
						n->tx_credits = 0U;
						n->tx_seq = 0U;
						n->rx_seq = 0U;
						n->usb_in_busy = false;
						n->credit_pending = false;
						n->adb_pending_syncs = 0U;
						n->adb_stale_drain = false;
						servo_bridge_clear_tx_buf(n);
						n->active_proto = new_proto;
						n->pending_reset_ack = true;
						n->pending_reset_ack_epoch =
							req_epoch;
						n->pending_reset_ack_boot =
							false;
						n->pending_reset_ack_proto =
							new_proto;
						if (fx != NULL) {
							fx->epoch_history_cleared =
								true;
							fx->cleared_epoch =
								req_epoch;
						}
					} else {
						/* ATmega Protocol Authority
						 * rejecting stale req_proto */
						n->epoch = req_epoch;
						n->active_proto = new_proto;
						n->usb_in_busy = false;
						n->credit_pending = false;
						n->adb_pending_syncs = 0U;
						n->adb_stale_drain = false;
						n->pending_reset_req = true;
						if (fx != NULL) {
							fx->epoch_history_cleared =
								true;
							fx->cleared_epoch =
								req_epoch;
						}
					}
				}
				/* Stale epoch: discard without rolling epoch
				 * backwards */
			}
		}

		if (fx != NULL && n->active_proto != old_proto) {
			fx->proto_changed = true;
			fx->new_active_proto = n->active_proto;
		}
		break;
	}

	case SERVO_FRAME_RESET_ACK: {
		uint8_t ack_epoch = frame_epoch;
		uint8_t ack_proto = arg_len & SERVO_RESET_PROTO_MASK;
		bool ack_boot = (arg_len & SERVO_RESET_FLAG_BOOT) != 0U;
		uint8_t old_proto = n->active_proto;

		if (n->state == SERVO_STATE_RESETTING &&
		    ack_boot == n->booted &&
		    (n->booted || ack_epoch == n->epoch)) {
			if (!n->is_epoch_leader) {
				if (ack_proto == n->active_proto) {
					n->state = SERVO_STATE_ACTIVE;
					n->epoch = ack_epoch;
					n->tx_credits = 1U;
					n->tx_seq = 0U;
					n->rx_seq = 0U;
					n->usb_in_busy = false;
					n->credit_pending = false;
					n->adb_pending_syncs = 0U;
					n->adb_stale_drain = false;
					servo_bridge_clear_tx_buf(n);
					n->booted = false;
					if (fx != NULL) {
						fx->epoch_history_cleared =
							true;
						fx->cleared_epoch = ack_epoch;
					}
					if (n->desired_proto !=
					    n->active_proto) {
						uint8_t saved_pre_suspend =
							n->pre_suspend_proto;

						(void)servo_bridge_set_proto(
							n, n->desired_proto,
							n->last_reset_tx_us);
						if (n->desired_proto ==
						    SERVO_PROTO_NONE) {
							n->pre_suspend_proto =
								saved_pre_suspend;
						}
					}
				}
			} else {
				n->state = SERVO_STATE_ACTIVE;
				n->epoch = ack_epoch;
				n->tx_credits = 1U;
				n->tx_seq = 0U;
				n->rx_seq = 0U;
				n->usb_in_busy = false;
				n->credit_pending = false;
				n->adb_pending_syncs = 0U;
				n->adb_stale_drain = false;
				n->active_proto = ack_proto;
				n->desired_proto = ack_proto;
				servo_bridge_clear_tx_buf(n);
				n->booted = false;
				if (fx != NULL) {
					fx->epoch_history_cleared = true;
					fx->cleared_epoch = ack_epoch;
				}
			}
		}

		if (fx != NULL && n->active_proto != old_proto) {
			fx->proto_changed = true;
			fx->new_active_proto = n->active_proto;
		}
		break;
	}

	case SERVO_FRAME_KEY_CMD:
		if (!n->is_epoch_leader) {
			uint8_t echo = (uint8_t)(~arg_len & 0xFFU);
			n->pending_key_ack = true;
			n->pending_key_ack_ch = echo;
			if (fx != NULL) {
				fx->key_cmd_received = true;
				fx->key_cmd_ch = arg_len;
			}
		}
		break;

	case SERVO_FRAME_KEY_ACK:
		if (n->is_epoch_leader) {
			if (fx != NULL) {
				fx->key_ack_received = true;
				fx->key_ack_ch = arg_len;
			}
		}
		break;

	default:
		break;
	}
}

/*
 * Unified RX Event Dispatcher:
 * Dispatches servo_frame_rx_step() outcomes (including non-swallowing 0xA5
 * timeout_occurred recovery) to servo_bridge_on_rx_frame() /
 * servo_bridge_on_corrupt_rx().
 */
static inline void servo_bridge_on_rx_event(struct servo_bridge_node *n,
					    enum servo_rx_event evt,
					    struct servo_bridge_rx_effects *fx)
{
	uint8_t i;

	if (fx != NULL) {
		fx->usb_in_armed = false;
		fx->usb_in_len = 0U;
		for (i = 0U; i < SERVO_MAX_DATA_LEN; ++i)
			fx->usb_in_payload[i] = 0U;
		fx->key_cmd_received = false;
		fx->key_cmd_ch = 0U;
		fx->key_ack_received = false;
		fx->key_ack_ch = 0U;
		fx->proto_changed = false;
		fx->new_active_proto = n->active_proto;
		fx->epoch_history_cleared = false;
		fx->cleared_epoch = 0U;
	}

	if (n->rx.timeout_occurred) {
		n->rx.timeout_occurred = false;
		servo_bridge_on_corrupt_rx(n);
	}

	switch (evt) {
	case SERVO_RX_EVT_FRAME_COMPLETE:
		servo_bridge_on_rx_frame(n, n->rx.type, n->rx.arg_len,
					 n->rx.payload, fx);
		break;
	case SERVO_RX_EVT_CRC_ERROR:
	case SERVO_RX_EVT_TIMEOUT_ERROR:
	case SERVO_RX_EVT_INVALID_HEADER:
		servo_bridge_on_corrupt_rx(n);
		break;
	case SERVO_RX_EVT_NONE:
	default:
		break;
	}
}

/*
 * TLA+ Actions: TimeoutRetransmitReset(n) / TimeoutRetransmitData(n)
 */
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

/*
 * Unified Periodic Timer Evaluator:
 * Evaluates both the 2 ms T_RX_FRAME whole-frame deadline and the 5 ms T_RET
 * RESET_REQ / DATA retransmission timers using wrap-safe unsigned subtraction.
 */
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
	    n->has_tx_data && !n->pending_data_tx &&
	    (uint32_t)(now_us - n->last_tx_us) >= SERVO_RETRANSMIT_TIMEOUT_US) {
		(void)servo_bridge_on_retransmit_timeout(n);
	}
}

/*
 * Priority 1 > Priority 2 > Priority 3 Outbound Frame Arbiter
 * Enforces SERVO_CTRL_TX_HEADROOM = 16B on Priority 2 and Priority 3 frames.
 */
static inline size_t
servo_bridge_pop_tx_frame(struct servo_bridge_node *n, size_t tx_ring_free,
			  uint8_t out_frame[SERVO_MAX_FRAME_BYTES],
			  uint32_t now_us)
{
	/* Priority 1: Reactive Control, Credit, & NAK frames (4B, no headroom
	 * needed) */
	if (n->pending_reset_req) {
		if (tx_ring_free >= 4U) {
			uint8_t arg =
				(uint8_t)((n->booted ? SERVO_RESET_FLAG_BOOT :
						       0x00U) |
					  (n->active_proto &
					   SERVO_RESET_PROTO_MASK));
			n->pending_reset_req = false;
			n->last_reset_tx_us = now_us;
			return servo_frame_encode(
				(uint8_t)(SERVO_FRAME_RESET_REQ |
					  (n->epoch & SERVO_EPOCH_MASK)),
				arg, NULL, out_frame);
		}
		return 0U;
	}

	if (n->pending_reset_ack) {
		if (tx_ring_free >= 4U) {
			uint8_t arg = (uint8_t)((n->pending_reset_ack_boot ?
							 SERVO_RESET_FLAG_BOOT :
							 0x00U) |
						(n->pending_reset_ack_proto &
						 SERVO_RESET_PROTO_MASK));
			uint8_t ep = n->pending_reset_ack_epoch &
				     SERVO_EPOCH_MASK;
			n->pending_reset_ack = false;
			return servo_frame_encode(
				(uint8_t)(SERVO_FRAME_RESET_ACK | ep), arg,
				NULL, out_frame);
		}
		return 0U;
	}

	if (n->pending_credit) {
		if (tx_ring_free >= 4U) {
			uint8_t ep = n->pending_credit_epoch & SERVO_EPOCH_MASK;
			uint8_t seq = n->pending_credit_seq & 0x01U;
			n->pending_credit = false;
			return servo_frame_encode((uint8_t)(SERVO_FRAME_CREDIT |
							    ep),
						  seq, NULL, out_frame);
		}
		return 0U;
	}

	if (n->pending_nak) {
		if (tx_ring_free >= 4U) {
			uint8_t ep = n->pending_nak_epoch & SERVO_EPOCH_MASK;
			uint8_t seq = n->pending_nak_seq & 0x01U;
			n->pending_nak = false;
			return servo_frame_encode((uint8_t)(SERVO_FRAME_NAK |
							    ep),
						  seq, NULL, out_frame);
		}
		return 0U;
	}

	/* Priority 2: Keyboard Multiplexing (4B + 16B headroom = 20B required)
	 */
	if (n->pending_key_cmd) {
		if (tx_ring_free >= SERVO_KEY_TX_MIN_FREE) {
			uint8_t ch = n->pending_key_cmd_ch;
			n->pending_key_cmd = false;
			return servo_frame_encode(SERVO_FRAME_KEY_CMD, ch, NULL,
						  out_frame);
		}
		return 0U;
	}

	if (n->pending_key_ack) {
		if (tx_ring_free >= SERVO_KEY_TX_MIN_FREE) {
			uint8_t ch = n->pending_key_ack_ch;
			n->pending_key_ack = false;
			return servo_frame_encode(SERVO_FRAME_KEY_ACK, ch, NULL,
						  out_frame);
		}
		return 0U;
	}

	/* Priority 3: Bulk Data (4B + len + 16B headroom <= 84B required) */
	if (n->state == SERVO_STATE_ACTIVE && n->pending_data_tx &&
	    n->has_tx_data) {
		if (tx_ring_free >= SERVO_DATA_TX_MIN_FREE(n->tx_data_len)) {
			uint8_t type = (uint8_t)(SERVO_FRAME_DATA |
						 ((n->tx_data_seq & 0x01U)
						  << SERVO_DATA_SEQ_SHIFT) |
						 (n->tx_data_epoch &
						  SERVO_EPOCH_MASK));
			n->pending_data_tx = false;
			n->last_tx_us = now_us;
			return servo_frame_encode(type, n->tx_data_len,
						  n->tx_data_buf, out_frame);
		}
	}

	return 0U;
}

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_SERVO_USB_BRIDGE_FRAME_H */
