/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB PRL module.
 */
#ifndef PLATFORM_EC_TEST_USB_PRL_H_
#define PLATFORM_EC_TEST_USB_PRL_H_

#include "common.h"

/* Protocol Transmit States (Section 6.11.2.2) */
enum usb_prl_tx_state {
	PRL_TX_PHY_LAYER_RESET,
	PRL_TX_WAIT_FOR_MESSAGE_REQUEST,
	PRL_TX_LAYER_RESET_FOR_TRANSMIT,
	PRL_TX_WAIT_FOR_PHY_RESPONSE,
	PRL_TX_SRC_SOURCE_TX,
	PRL_TX_SNK_START_AMS,
	PRL_TX_SRC_PENDING,
	PRL_TX_SNK_PENDING,
	PRL_TX_DISCARD_MESSAGE,
};

/* Protocol Hard Reset States (Section 6.11.2.4) */
enum usb_prl_hr_state {
	PRL_HR_WAIT_FOR_REQUEST,
	PRL_HR_RESET_LAYER,
	PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE,
	PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE,
};

/* Chunked Rx states (Section 6.11.2.1.2) */
enum usb_rch_state {
	RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER,
	RCH_PASS_UP_MESSAGE,
	RCH_PROCESSING_EXTENDED_MESSAGE,
	RCH_REQUESTING_CHUNK,
	RCH_WAITING_CHUNK,
	RCH_REPORT_ERROR,
};

/* Chunked Tx states (Section 6.11.2.1.3) */
enum usb_tch_state {
	TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE,
	TCH_WAIT_FOR_TRANSMISSION_COMPLETE,
	TCH_CONSTRUCT_CHUNKED_MESSAGE,
	TCH_SENDING_CHUNKED_MESSAGE,
	TCH_WAIT_CHUNK_REQUEST,
	TCH_MESSAGE_RECEIVED,
	TCH_MESSAGE_SENT,
	TCH_REPORT_ERROR,
};

enum usb_prl_tx_state prl_tx_get_state(const int port);
enum usb_prl_hr_state prl_hr_get_state(const int port);
enum usb_rch_state rch_get_state(const int port);
enum usb_tch_state tch_get_state(const int port);

#endif /* PLATFORM_EC_TEST_USB_PRL_H_ */
