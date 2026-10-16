/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test that RDD and CCD inhibit deep sleep (SLEEP_MASK_RDD) while a debug
 * cable is attached or AP UART is active, preventing Cr50 from powering down
 * the USB PHY in G3 when the host suspends the USB bus (b/574267807).
 */

#include "case_closed_debug.h"
#include "ccd_config.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "ec_comm.h"
#include "gpio.h"
#include "hooks.h"
#include "rdd.h"
#include "system.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "uart_bitbang.h"
#include "uartn.h"
#include "usb_api.h"
#include "usb_console.h"
#include "usb_i2c.h"
#include "usb_spi.h"

static struct usb_spi_state mock_usb_spi_state;
struct usb_spi_config const ccd_usb_spi = {
	.state = &mock_usb_spi_state,
};

static int mock_uart_enabled[UART_COUNT];
static int mock_ap_on;
static int mock_ap_uart_on;
static int mock_usb_inited;

const char *device_state_name(enum device_state state)
{
	return "mock";
}

int ccd_is_cap_enabled(enum ccd_capability cap)
{
	return 1;
}

int ccd_get_flag(enum ccd_flag flag)
{
	return 0;
}

int ccd_set_flag(enum ccd_flag flag, int value)
{
	return EC_SUCCESS;
}

void clock_enable_module(enum module_id module, int enable)
{
}

void delay_sleep_by(uint32_t us)
{
}

void task_enable_irq(int irq)
{
}

void task_start_irq_handler(void *excep_return)
{
}

void task_resched_if_needed(void *excep_return)
{
}

int uartn_is_enabled(int uart)
{
	return mock_uart_enabled[uart];
}

void uartn_enable(int uart)
{
	mock_uart_enabled[uart] = 1;
}

void uartn_disable(int uart)
{
	mock_uart_enabled[uart] = 0;
}

int uart_bitbang_is_enabled(void)
{
	return 0;
}

int usb_i2c_board_is_enabled(void)
{
	return 0;
}

int usb_i2c_board_enable(void)
{
	return EC_SUCCESS;
}

void usb_i2c_board_disable(void)
{
}

int uart_ec_bridge_is_enabled(void)
{
	return 0;
}

int uart_ec_bridge_tx_is_enabled(void)
{
	return 0;
}

void uart_ec_bridge_enable(int enable_rx, int enable_tx)
{
}

int ap_is_on(void)
{
	return mock_ap_on;
}

int ap_uart_is_on(void)
{
	return mock_ap_uart_on;
}

int ec_is_rx_allowed(void)
{
	return 0;
}

int servo_is_connected(void)
{
	return 0;
}

void servo_ignore(int enable)
{
}

int ec_comm_is_uart_in_packet_mode(int uart)
{
	return 0;
}

void ec_comm_block(int block)
{
}

void usb_spi_enable(struct usb_spi_config const *config, int enabled)
{
	config->state->enabled_device = enabled;
}

void usb_init(void)
{
	mock_usb_inited = 1;
	disable_sleep(SLEEP_MASK_USB_DEVICE);
}

void usb_release(void)
{
	mock_usb_inited = 0;
	enable_sleep(SLEEP_MASK_USB_DEVICE);
}

void usb_console_enable(int enabled, int is_readonly)
{
}

void print_ap_state(void)
{
}

void print_ap_uart_state(void)
{
}

void print_ec_state(void)
{
}

void print_pcr0(void)
{
}

void print_servo_state(void)
{
}

void test_init(void)
{
	/*
	 * CCD_MODE_L is active-low; initialize it high (disconnected) before
	 * tasks and HOOK_SECOND start.
	 */
	gpio_set_level(GPIO_CCD_MODE_L, 1);
}

static int test_rdd_connect_and_usb_suspend_in_g3(void)
{
	/*
	 * Baseline: AP off (G3), no debug cable attached. Deep sleep must be
	 * allowed.
	 */
	mock_ap_on = 0;
	mock_ap_uart_on = 0;
	TEST_ASSERT(gpio_get_level(GPIO_CCD_MODE_L) == 1);
	TEST_ASSERT(!ccd_ext_is_enabled());
	TEST_ASSERT(!(sleep_mask & SLEEP_MASK_RDD));
	TEST_ASSERT(DEEP_SLEEP_ALLOWED);

	/*
	 * 1. Debug cable attaches: rdd_connect() asserts CCD_MODE_L and
	 * immediately sets SLEEP_MASK_RDD even before the next HOOK_SECOND
	 * runs ccd_ext_detect().
	 */
	rdd_connect();
	TEST_ASSERT(gpio_get_level(GPIO_CCD_MODE_L) == 0);
	TEST_ASSERT(!ccd_ext_is_enabled());
	TEST_ASSERT(sleep_mask & SLEEP_MASK_RDD);
	TEST_ASSERT(!DEEP_SLEEP_ALLOWED);

	/*
	 * 2. HOOK_SECOND runs ccd_ext_detect(), enabling CCD and initializing
	 * USB.
	 */
	hook_notify(HOOK_SECOND);
	msleep(10);
	TEST_ASSERT(ccd_ext_is_enabled());
	TEST_ASSERT(mock_usb_inited);
	TEST_ASSERT(sleep_mask & SLEEP_MASK_USB_DEVICE);
	TEST_ASSERT(sleep_mask & SLEEP_MASK_RDD);
	TEST_ASSERT(!DEEP_SLEEP_ALLOWED);

	/*
	 * 3. Simulate USB bus idle > 3ms in G3 (USBSUSP interrupt in
	 * chip/g/usb.c clears SLEEP_MASK_USB_DEVICE). SLEEP_MASK_RDD must
	 * remain set so Cr50 does not enter deep sleep and power down the USB
	 * PHY while the debug cable is still attached.
	 */
	enable_sleep(SLEEP_MASK_USB_DEVICE);
	TEST_ASSERT(!(sleep_mask & SLEEP_MASK_USB_DEVICE));
	TEST_ASSERT(sleep_mask & SLEEP_MASK_RDD);
	TEST_ASSERT(!DEEP_SLEEP_ALLOWED);

	/*
	 * 4. Debug cable detaches: rdd_disconnect() releases CCD_MODE_L, but
	 * because ccd_ext_is_enabled() is still true until ccd_ext_detect()
	 * runs, SLEEP_MASK_RDD stays held until CCD teardown completes.
	 */
	rdd_disconnect();
	TEST_ASSERT(gpio_get_level(GPIO_CCD_MODE_L) == 1);
	TEST_ASSERT(ccd_ext_is_enabled());
	TEST_ASSERT(sleep_mask & SLEEP_MASK_RDD);
	TEST_ASSERT(!DEEP_SLEEP_ALLOWED);

	/*
	 * 5. HOOK_SECOND runs ccd_ext_detect(), tearing down CCD and clearing
	 * SLEEP_MASK_RDD so deep sleep is allowed again in G3.
	 */
	hook_notify(HOOK_SECOND);
	msleep(10);
	TEST_ASSERT(!ccd_ext_is_enabled());
	TEST_ASSERT(!mock_usb_inited);
	TEST_ASSERT(!(sleep_mask & SLEEP_MASK_RDD));
	TEST_ASSERT(DEEP_SLEEP_ALLOWED);

	/*
	 * 6. Transient attach/detach before HOOK_SECOND runs ccd_ext_detect():
	 * rdd_connect() sets SLEEP_MASK_RDD, and rdd_disconnect() clears it
	 * immediately since ccd_ext_is_enabled() and UART_AP are both off.
	 */
	rdd_connect();
	TEST_ASSERT(sleep_mask & SLEEP_MASK_RDD);
	TEST_ASSERT(!DEEP_SLEEP_ALLOWED);
	rdd_disconnect();
	TEST_ASSERT(!(sleep_mask & SLEEP_MASK_RDD));
	TEST_ASSERT(DEEP_SLEEP_ALLOWED);

	return EC_SUCCESS;
}

static int test_ap_uart_interaction_with_rdd_sleep_mask(void)
{
	/* Attach debug cable and enable CCD. */
	rdd_connect();
	hook_notify(HOOK_SECOND);
	msleep(10);
	TEST_ASSERT(ccd_ext_is_enabled());
	TEST_ASSERT(sleep_mask & SLEEP_MASK_RDD);

	/* Simulate USB suspend so only SLEEP_MASK_RDD is tested. */
	enable_sleep(SLEEP_MASK_USB_DEVICE);

	/* Turn on AP and AP UART; ccd_state_change_hook enables UART_AP. */
	mock_ap_on = 1;
	mock_ap_uart_on = 1;
	ccd_update_state();
	msleep(10);
	TEST_ASSERT(uartn_is_enabled(UART_AP));
	TEST_ASSERT(sleep_mask & SLEEP_MASK_RDD);

	/*
	 * Case A: AP turns off while debug cable remains attached.
	 * Disabling UART_AP must NOT clear SLEEP_MASK_RDD while CCD is active.
	 */
	mock_ap_on = 0;
	mock_ap_uart_on = 0;
	ccd_update_state();
	msleep(10);
	TEST_ASSERT(!uartn_is_enabled(UART_AP));
	TEST_ASSERT(ccd_ext_is_enabled());
	TEST_ASSERT(sleep_mask & SLEEP_MASK_RDD);
	TEST_ASSERT(!DEEP_SLEEP_ALLOWED);

	/*
	 * Case B: Re-enable AP UART, then detach debug cable while AP UART is
	 * still enabled. ccd_ext_detect() disables CCD first (while UART_AP is
	 * still enabled) and schedules deferred ccd_state_change_hook() to
	 * disable UART_AP.
	 */
	mock_ap_on = 1;
	mock_ap_uart_on = 1;
	ccd_update_state();
	msleep(10);
	TEST_ASSERT(uartn_is_enabled(UART_AP));

	rdd_disconnect();
	hook_notify(HOOK_SECOND);
	/*
	 * Immediately after ccd_ext_detect() (before deferred
	 * ccd_state_change_hook runs), UART_AP is still enabled so
	 * SLEEP_MASK_RDD must remain set.
	 */
	TEST_ASSERT(!ccd_ext_is_enabled());
	TEST_ASSERT(uartn_is_enabled(UART_AP));
	TEST_ASSERT(sleep_mask & SLEEP_MASK_RDD);

	/*
	 * Once deferred ccd_state_change_hook runs, UART_AP is disabled and
	 * SLEEP_MASK_RDD is finally cleared.
	 */
	msleep(10);
	TEST_ASSERT(!uartn_is_enabled(UART_AP));
	TEST_ASSERT(!(sleep_mask & SLEEP_MASK_RDD));
	TEST_ASSERT(DEEP_SLEEP_ALLOWED);

	mock_ap_on = 0;
	mock_ap_uart_on = 0;
	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_rdd_connect_and_usb_suspend_in_g3);
	RUN_TEST(test_ap_uart_interaction_with_rdd_sleep_mask);

	test_print_result();
}
