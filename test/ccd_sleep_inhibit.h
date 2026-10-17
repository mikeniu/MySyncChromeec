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

#ifdef TEST_CCD_SLEEP_INHIBIT_MOCKS
#include "case_closed_debug.h"
#include "ccd_config.h"
#include "clock.h"
#include "common.h"
#include "ec_comm.h"
#include "gpio.h"
#include "system.h"
#include "task.h"
#include "uart_bitbang.h"
#include "usb_api.h"
#include "usb_spi.h"

static struct usb_spi_state mock_usb_spi_state;
struct usb_spi_config const ccd_usb_spi = { .state = &mock_usb_spi_state };
static int mock_uart_enabled[UART_COUNT];
static int mock_ap_on, mock_ap_uart_on;

const char *device_state_name(enum device_state state) { return "mock"; }
int ccd_is_cap_enabled(enum ccd_capability cap) { return 1; }
int ccd_get_flag(enum ccd_flag flag) { return 0; }
int ccd_set_flag(enum ccd_flag flag, int value) { return EC_SUCCESS; }
void clock_enable_module(enum module_id module, int enable) {}
void delay_sleep_by(uint32_t us) {}
void task_enable_irq(int irq) {}
void task_start_irq_handler(void *excep_return) {}
void task_resched_if_needed(void *excep_return) {}
int uartn_is_enabled(int uart) { return mock_uart_enabled[uart]; }
void uartn_enable(int uart) { mock_uart_enabled[uart] = 1; }
void uartn_disable(int uart) { mock_uart_enabled[uart] = 0; }
int uart_bitbang_is_enabled(void) { return 0; }
int usb_i2c_board_is_enabled(void) { return 0; }
int usb_i2c_board_enable(void) { return EC_SUCCESS; }
void usb_i2c_board_disable(void) {}
int uart_ec_bridge_is_enabled(void) { return 0; }
int uart_ec_bridge_tx_is_enabled(void) { return 0; }
void uart_ec_bridge_enable(int enable_rx, int enable_tx) {}
int ap_is_on(void) { return mock_ap_on; }
int ap_uart_is_on(void) { return mock_ap_uart_on; }
int ec_is_rx_allowed(void) { return 0; }
int servo_is_connected(void) { return 0; }
void servo_ignore(int enable) {}
int ec_comm_is_uart_in_packet_mode(int uart) { return 0; }
void ec_comm_block(int block) {}
void usb_spi_enable(struct usb_spi_config const *config, int enabled)
{
	config->state->enabled_device = enabled;
}
void usb_init(void) { disable_sleep(SLEEP_MASK_USB_DEVICE); }
void usb_release(void) { enable_sleep(SLEEP_MASK_USB_DEVICE); }
void usb_console_enable(int enabled, int is_readonly) {}
void print_ap_state(void) {}
void print_ap_uart_state(void) {}
void print_ec_state(void) {}
void print_pcr0(void) {}
void print_servo_state(void) {}
void test_init(void) { gpio_set_level(GPIO_CCD_MODE_L, 1); }
#endif /* TEST_CCD_SLEEP_INHIBIT_MOCKS */
