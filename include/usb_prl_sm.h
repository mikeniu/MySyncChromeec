/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Protocol Layer module */

#ifndef __CROS_EC_USB_PRL_H
#define __CROS_EC_USB_PRL_H
#include "common.h"
#include "usb_pd.h"

/**
 * Initialize the Protocol Layer State Machine
 *
 * @param port USB-C port number
 */
void prl_init(int port);

/**
 * Runs the Protocol Layer State Machine
 *
 * @param port USB-C port number
 * @param evt  system event, ie: PD_EVENT_RX
 * @param en   0 to disable the machine, 1 to enable the machine
 */
void protocol_layer(int port, int evt, int en);

/**
 * Sends a PD control message
 *
 * @param port USB-C port number
 * @param type Transmit type
 * @param msg  Control message type
 */
void prl_send_ctrl_msg(int port, enum tcpm_transmit_type type,
	enum pd_ctrl_msg_type msg);

/**
 * Sends a PD data message
 *
 * @param port USB-C port number
 * @param type Transmit type
 * @param msg  Data message type
 */
void prl_send_data_msg(int port, enum tcpm_transmit_type type,
	enum pd_data_msg_type msg);

/**
 * Sends a PD extended data message
 *
 * @param port USB-C port number
 * @param type Transmit type
 * @param msg  Extended data message type
 */
void prl_send_ext_data_msg(int port, enum tcpm_transmit_type type,
	enum pd_ext_msg_type msg);

/**
 * Informs the Protocol Layer that a hard reset has completed
 *
 * @param port USB-C port number
 */
void prl_hard_reset_complete(int port);

/**
 * Informs the Protocol Layer to start an Atomic Message Sequence
 *
 * @param port USB-C port number
 */
void prl_start_ams(int port);

/**
 * Informs the Protocol Layer to end an Atomic Message Sequence
 *
 * @param port USB-C port number
 */
void prl_end_ams(int port);

#endif /* __CROS_EC_USB_PRL_H */

