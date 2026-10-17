/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TEST_CCD_SLEEP_INHIBIT_H
#define __CROS_EC_TEST_CCD_SLEEP_INHIBIT_H

enum device_state {
	DEVICE_STATE_INIT = 0, DEVICE_STATE_INIT_DEBOUNCING,
	DEVICE_STATE_INIT_RX_ONLY, DEVICE_STATE_DISCONNECTED,
	DEVICE_STATE_OFF, DEVICE_STATE_UNDETECTABLE,
	DEVICE_STATE_CONNECTED, DEVICE_STATE_ON,
	DEVICE_STATE_DEBOUNCING, DEVICE_STATE_UNKNOWN,
	DEVICE_STATE_IGNORED, DEVICE_STATE_COUNT
};
const char *device_state_name(enum device_state state);
void servo_ignore(int enable);
int usb_i2c_board_enable(void);
void usb_i2c_board_disable(void);
void usb_console_enable(int enabled, int readonly);
int uartn_is_enabled(int uart);
void uartn_enable(int uart);
void uartn_disable(int uart);
void print_ap_state(void);
void print_ap_uart_state(void);
void print_ec_state(void);
void print_pcr0(void);
void print_servo_state(void);
int ap_is_on(void);
int ap_uart_is_on(void);
int ec_is_rx_allowed(void);
int servo_is_connected(void);

#endif /* __CROS_EC_TEST_CCD_SLEEP_INHIBIT_H */
