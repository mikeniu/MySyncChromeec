/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "case_closed_debug.h"
#include "ccd_config.h"
#include "ccd_sleep_inhibit.h"
#include "gpio.h"
#include "hooks.h"
#include "rdd.h"
#include "system.h"
#include "test_util.h"
#include "timer.h"
#include "usb_api.h"
#include "usb_spi.h"

static const int STATE_UPDATE_DELAY_MS = 10;
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
void usb_spi_enable(struct usb_spi_config const *c, int en) {}
void usb_init(void) { disable_sleep(SLEEP_MASK_USB_DEVICE); }
void usb_release(void) { enable_sleep(SLEEP_MASK_USB_DEVICE); }
void usb_console_enable(int enabled, int is_readonly) {}
void print_ap_state(void) {}
void print_ap_uart_state(void) {}
void print_ec_state(void) {}
void print_pcr0(void) {}
void print_servo_state(void) {}
void test_init(void) { gpio_set_level(GPIO_CCD_MODE_L, 1); }

static void set_ap_uart_and_wait(int ap_on, int uart_on)
{
	mock_ap_on = ap_on;
	mock_ap_uart_on = uart_on;
	ccd_update_state();
	msleep(STATE_UPDATE_DELAY_MS);
}

static int test_rdd_connect_and_usb_suspend_in_g3(void)
{
	TEST_ASSERT(gpio_get_level(GPIO_CCD_MODE_L) && DEEP_SLEEP_ALLOWED);
	rdd_connect();
	TEST_ASSERT(!gpio_get_level(GPIO_CCD_MODE_L) && !ccd_ext_is_enabled());
	TEST_ASSERT((sleep_mask & SLEEP_MASK_RDD) && !DEEP_SLEEP_ALLOWED);
	hook_notify(HOOK_SECOND);
	msleep(STATE_UPDATE_DELAY_MS);
	TEST_ASSERT(ccd_ext_is_enabled() && (sleep_mask & SLEEP_MASK_RDD));
	TEST_ASSERT(sleep_mask & SLEEP_MASK_USB_DEVICE);
	enable_sleep(SLEEP_MASK_USB_DEVICE);
	TEST_ASSERT(sleep_mask == SLEEP_MASK_RDD && !DEEP_SLEEP_ALLOWED);
	rdd_disconnect();
	TEST_ASSERT(gpio_get_level(GPIO_CCD_MODE_L) && ccd_ext_is_enabled());
	TEST_ASSERT((sleep_mask & SLEEP_MASK_RDD) && !DEEP_SLEEP_ALLOWED);
	hook_notify(HOOK_SECOND);
	msleep(STATE_UPDATE_DELAY_MS);
	TEST_ASSERT(!ccd_ext_is_enabled() && DEEP_SLEEP_ALLOWED);
	rdd_connect();
	TEST_ASSERT((sleep_mask & SLEEP_MASK_RDD) && !DEEP_SLEEP_ALLOWED);
	rdd_disconnect();
	TEST_ASSERT(!(sleep_mask & SLEEP_MASK_RDD) && DEEP_SLEEP_ALLOWED);
	return EC_SUCCESS;
}

static int test_ap_uart_interaction_with_rdd_sleep_mask(void)
{
	rdd_connect();
	hook_notify(HOOK_SECOND);
	msleep(STATE_UPDATE_DELAY_MS);
	enable_sleep(SLEEP_MASK_USB_DEVICE);
	set_ap_uart_and_wait(1, 1);
	TEST_ASSERT(uartn_is_enabled(UART_AP) && (sleep_mask & SLEEP_MASK_RDD));
	set_ap_uart_and_wait(0, 0);
	TEST_ASSERT(!uartn_is_enabled(UART_AP) && ccd_ext_is_enabled());
	TEST_ASSERT((sleep_mask & SLEEP_MASK_RDD) && !DEEP_SLEEP_ALLOWED);
	set_ap_uart_and_wait(1, 1);
	rdd_disconnect();
	hook_notify(HOOK_SECOND);
	TEST_ASSERT(!ccd_ext_is_enabled() && uartn_is_enabled(UART_AP));
	TEST_ASSERT((sleep_mask & SLEEP_MASK_RDD) && !DEEP_SLEEP_ALLOWED);
	msleep(STATE_UPDATE_DELAY_MS);
	TEST_ASSERT(!uartn_is_enabled(UART_AP) && DEEP_SLEEP_ALLOWED);
	set_ap_uart_and_wait(0, 0);
	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();
	RUN_TEST(test_rdd_connect_and_usb_suspend_in_g3);
	RUN_TEST(test_ap_uart_interaction_with_rdd_sleep_mask);
	test_print_result();
}
