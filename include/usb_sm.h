/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB State Machine Framework */

#ifndef __CROS_EC_USB_SM_H
#define __CROS_EC_USB_SM_H

#define SM_FLAGS_MSG_RECEIVED            (1 << 0)
#define SM_FLAGS_REJECT                  (1 << 1)
#define SM_FLAGS_WAIT                    (1 << 2)
#define SM_FLAGS_CHUNKING                (1 << 3)
#define SM_FLAGS_ABORT                   (1 << 4)
#define SM_FLAGS_TX_COMPLETE             (1 << 5)
#define SM_FLAGS_START_AMS               (1 << 6)
#define SM_FLAGS_END_AMS                 (1 << 7)
#define SM_FLAGS_TX_ERROR                (1 << 8)
#define SM_FLAGS_PE_HARD_RESET           (1 << 9)
#define SM_FLAGS_HARD_RESET_COMPLETE     (1 << 10)
#define SM_FLAGS_PORT_PARTNER_HARD_RESET (1 << 11)
#define SM_FLAGS_MSG_XMIT                (1 << 12)
#define SM_UPDATE_REMOTE_CAPS            (1 << 13)
#define SM_FLAGS_PREVIOUS_PD_CONN        (1 << 14)
#define SM_FLAGS_PARTNER_DR_POWER        (1 << 15)
#define SM_FLAGS_PARTNER_EXTPOWER        (1 << 16)
#define SM_FLAGS_PARTNER_USB_COMM        (1 << 17)
#define SM_FLAGS_PARTNER_DR_DATA         (1 << 18)
#define SM_FLAGS_EXPLICIT_CONTRACT       (1 << 19)
#define SM_FLAGS_POWER_ROLE_SWAP         (1 << 20)
#define SM_FLAGS_CONTRACT_INVALID        (1 << 21)
#define SM_FLAGS_PD_ENABLE               (1 << 22)

#define SM_OBJ(smo)    ((struct sm_obj *)&smo)
#define SUPER(sig, s)  ((((sig) == ENTRY_SIG) || ((sig) == EXIT_SIG)) \
					? 0 : ((unsigned int)(s)))
#define SET_STATE(p, o, t)   {set_state(p, o, t); return 0; }
#define STATE_RETURN   return(0)
#define RUN_SIG_DONE   return(0)
#define RUN_SUPER      break

/* State Machine signals */
enum {
	ENTRY_SIG = 0,
	RUN_SIG,
	EXIT_SIG,
	SUPER_SIG,
};

typedef unsigned int (*sm_state)(int port, int sig);

struct sm_obj {
	sm_state task_state;
	sm_state last_state;
};

/**
 * Initialize a State Machine
 *
 * @param port   USB-C port number
 * @param obj    State machine object
 * @param target Initial state of state machine
 */
void init_state(int port, struct sm_obj *obj, sm_state target);

/**
 * Changes a state machines state
 *
 * @param port   USB-C port number
 * @param obj    State machine object
 * @param target State to transition to
 */
void set_state(int port, struct sm_obj *obj, sm_state target);

/**
 * Executes a state macine
 *
 * @param port USB-C port number
 * @param obj  State machine object
 * @param sig  State machine signal
 */
void exe_state(int port, struct sm_obj *obj, int sig);

#endif /* __CROS_EC_USB_SM_H */
