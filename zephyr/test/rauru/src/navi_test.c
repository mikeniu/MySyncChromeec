/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "ap_power/ap_power_events.h"
#include "cros_board_info.h"
#include "ec_commands.h"
#include "hooks.h"
#include "power.h"
#include "power/mt8186.h"
#include "test_state.h"
#include "timer.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(suspend_resume_hack, LOG_LEVEL_INF);

#define PREINIT_VERSION 0xDEADDEAD
#define PS8802_MUX USB_MUX_STRUCT_NAME(DT_NODELABEL(ps8802_mux_0))

extern uint32_t board_version;
extern struct gpio_callback cs_callback;
extern void enable_cs_interrupt(void);
extern void disable_cs_interrupt(void);
extern void ap_wakeup_isr_debounce(void);
extern void ap_wakeup_isr(const struct device *port, struct gpio_callback *cb,
			  gpio_port_pins_t pins);
extern void init_suspend_resume_workaround(void);
extern void setup_mux_config(void);

extern const struct deferred_data enable_cs_interrupt_data;
extern const struct deferred_data ap_wakeup_isr_debounce_data;

static uint32_t mock_board_version = 1;
static int mock_cbi_return_val = 0;

int cbi_get_board_version(uint32_t *version)
{
	if (mock_cbi_return_val != 0) {
		return mock_cbi_return_val;
	}
	*version = mock_board_version;
	return 0;
}

static void test_navi_before(void *data)
{
	ARG_UNUSED(data);

	hook_call_deferred(&enable_cs_interrupt_data, -1);
	hook_call_deferred(&ap_wakeup_isr_debounce_data, -1);

	board_version = PREINIT_VERSION;
	mock_board_version = 1;
	mock_cbi_return_val = 0;

	const struct gpio_dt_spec cs_gpio =
		GPIO_DT_SPEC_GET(DT_NODELABEL(shi0), cs_gpios);
	gpio_pin_configure_dt(&cs_gpio, GPIO_INPUT);
	gpio_emul_input_set(cs_gpio.port, cs_gpio.pin, 1);
	gpio_init_callback(&cs_callback, ap_wakeup_isr, BIT(cs_gpio.pin));

	const struct gpio_dt_spec *s3_l =
		GPIO_DT_FROM_NODELABEL(gpio_ap_in_sleep_l);
	gpio_pin_configure_dt(s3_l, GPIO_INPUT);
}

ZTEST(navi_test, test_init_suspend_resume_workaround_cbi_fail)
{
	mock_cbi_return_val = -1;
	init_suspend_resume_workaround();
	zassert_equal(board_version, PREINIT_VERSION);
}

ZTEST(navi_test, test_init_suspend_resume_workaround_newer_board)
{
	mock_board_version = 2;
	init_suspend_resume_workaround();
	zassert_equal(board_version, 2);

	/* Calling again when board_version is already initialized */
	mock_cbi_return_val = -1;
	mock_board_version = 99;
	init_suspend_resume_workaround();
	zassert_equal(board_version, 2);
}

ZTEST(navi_test, test_init_suspend_resume_workaround_early_board)
{
	mock_board_version = 1;
	init_suspend_resume_workaround();
	zassert_equal(board_version, 1);

	/* Calling again when board_version is already <= 1 */
	mock_cbi_return_val = -1;
	mock_board_version = 99;
	init_suspend_resume_workaround();
	zassert_equal(board_version, 1);
}

ZTEST(navi_test, test_host_sleep_event)
{
	/* For board_version > 1, ignore sleep event */
	board_version = 2;
	board_handle_host_sleep_event(HOST_SLEEP_EVENT_S3_SUSPEND);

	/* For board_version <= 1 */
	board_version = 1;
	board_handle_host_sleep_event(HOST_SLEEP_EVENT_S3_RESUME);
	board_handle_host_sleep_event(HOST_SLEEP_EVENT_S3_SUSPEND);
	k_msleep(55);
}

ZTEST(navi_test, test_enable_disable_cs_interrupt)
{
	const struct gpio_dt_spec cs_gpio =
		GPIO_DT_SPEC_GET(DT_NODELABEL(shi0), cs_gpios);
	const struct gpio_dt_spec *s3_l =
		GPIO_DT_FROM_NODELABEL(gpio_ap_in_sleep_l);
	gpio_flags_t flags;

	/* When CS is low, enable_cs_interrupt triggers ap_wakeup_isr */
	gpio_emul_input_set(cs_gpio.port, cs_gpio.pin, 0);
	enable_cs_interrupt();

	zassert_ok(gpio_emul_flags_get(s3_l->port, s3_l->pin, &flags));
	zassert_true(flags & GPIO_OUTPUT);

	disable_cs_interrupt();

	/* When CS is high, enable_cs_interrupt does not trigger ap_wakeup_isr
	 */
	gpio_pin_configure_dt(s3_l, GPIO_INPUT);
	gpio_emul_input_set(cs_gpio.port, cs_gpio.pin, 1);
	enable_cs_interrupt();
	zassert_ok(gpio_emul_flags_get(s3_l->port, s3_l->pin, &flags));
	zassert_true(flags & GPIO_INPUT);
	disable_cs_interrupt();
}

ZTEST(navi_test, test_ap_wakeup_isr_and_debounce)
{
	const struct gpio_dt_spec cs_gpio =
		GPIO_DT_SPEC_GET(DT_NODELABEL(shi0), cs_gpios);
	const struct gpio_dt_spec *s3_l =
		GPIO_DT_FROM_NODELABEL(gpio_ap_in_sleep_l);
	gpio_flags_t flags;

	/* CS low: ap_wakeup_isr sets s3_indicator_l to OUTPUT_LOW */
	gpio_emul_input_set(cs_gpio.port, cs_gpio.pin, 0);
	ap_wakeup_isr(cs_gpio.port, NULL, cs_gpio.pin);
	zassert_ok(gpio_emul_flags_get(s3_l->port, s3_l->pin, &flags));
	zassert_true(flags & GPIO_OUTPUT);

	/* ap_wakeup_isr_debounce when CS is still low does nothing */
	ap_wakeup_isr_debounce();
	zassert_ok(gpio_emul_flags_get(s3_l->port, s3_l->pin, &flags));
	zassert_true(flags & GPIO_OUTPUT);

	/* CS high: ap_wakeup_isr defers debounce */
	gpio_emul_input_set(cs_gpio.port, cs_gpio.pin, 1);
	ap_wakeup_isr(cs_gpio.port, NULL, cs_gpio.pin);

	/* ap_wakeup_isr_debounce when CS is high resets s3_indicator_l to INPUT
	 */
	ap_wakeup_isr_debounce();
	zassert_ok(gpio_emul_flags_get(s3_l->port, s3_l->pin, &flags));
	zassert_true(flags & GPIO_INPUT);

	k_msleep(55);
}

ZTEST(navi_test, test_navi_power_event_handler)
{
	const struct gpio_dt_spec *s3_l =
		GPIO_DT_FROM_NODELABEL(gpio_ap_in_sleep_l);
	gpio_flags_t flags;

	/* Board version > 1 returns early */
	gpio_pin_configure_dt(s3_l, GPIO_OUTPUT_LOW);
	board_version = 2;
	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	zassert_ok(gpio_emul_flags_get(s3_l->port, s3_l->pin, &flags));
	zassert_true(flags & GPIO_OUTPUT);

	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
	zassert_ok(gpio_emul_flags_get(s3_l->port, s3_l->pin, &flags));
	zassert_true(flags & GPIO_OUTPUT);

	/* Board version <= 1 */
	board_version = 1;

	/* Default/unhandled event does nothing */
	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	zassert_ok(gpio_emul_flags_get(s3_l->port, s3_l->pin, &flags));
	zassert_true(flags & GPIO_OUTPUT);

	/* SHUTDOWN resets s3_indicator_l to INPUT */
	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
	zassert_ok(gpio_emul_flags_get(s3_l->port, s3_l->pin, &flags));
	zassert_true(flags & GPIO_INPUT);

	/* RESUME_INIT resets s3_indicator_l to INPUT */
	gpio_pin_configure_dt(s3_l, GPIO_OUTPUT_LOW);
	ap_power_ev_send_callbacks(AP_POWER_RESUME_INIT);
	zassert_ok(gpio_emul_flags_get(s3_l->port, s3_l->pin, &flags));
	zassert_true(flags & GPIO_INPUT);
}

ZTEST(navi_test, test_board_handle_sleep_hang)
{
	const struct gpio_dt_spec *s3_l =
		GPIO_DT_FROM_NODELABEL(gpio_ap_in_sleep_l);
	gpio_flags_t flags;

	/* S0IX_SUSPEND resets to INPUT */
	gpio_pin_configure_dt(s3_l, GPIO_OUTPUT_LOW);
	board_handle_sleep_hang(SLEEP_HANG_S0IX_SUSPEND);
	zassert_ok(gpio_emul_flags_get(s3_l->port, s3_l->pin, &flags));
	zassert_true(flags & GPIO_INPUT);

	/* S0IX_RESUME resets to INPUT */
	gpio_pin_configure_dt(s3_l, GPIO_OUTPUT_LOW);
	board_handle_sleep_hang(SLEEP_HANG_S0IX_RESUME);
	zassert_ok(gpio_emul_flags_get(s3_l->port, s3_l->pin, &flags));
	zassert_true(flags & GPIO_INPUT);

	/* Other hang logs warning and doesn't change state */
	gpio_pin_configure_dt(s3_l, GPIO_OUTPUT_LOW);
	board_handle_sleep_hang(SLEEP_HANG_NONE);
	zassert_ok(gpio_emul_flags_get(s3_l->port, s3_l->pin, &flags));
	zassert_true(flags & GPIO_OUTPUT);
}

ZTEST(navi_test, test_setup_mux_config)
{
	const struct usb_mux *orig_mux = usb_muxes[1].mux;
	zassert_not_equal(orig_mux, &PS8802_MUX);

	/* CBI failure -> no change */
	mock_cbi_return_val = -1;
	setup_mux_config();
	zassert_equal(usb_muxes[1].mux, orig_mux);

	/* Board version != 0 -> no change */
	mock_cbi_return_val = 0;
	mock_board_version = 1;
	setup_mux_config();
	zassert_equal(usb_muxes[1].mux, orig_mux);

	/* Board version == 0 -> usb_muxes[1].mux updated to PS8802_MUX */
	mock_board_version = 0;
	setup_mux_config();
	zassert_equal(usb_muxes[1].mux, &PS8802_MUX);

	/* Restore original mux */
	usb_muxes[1].mux = orig_mux;
}

ZTEST_SUITE(navi_test, rauru_predicate_post_main, NULL, test_navi_before,
	    test_navi_before, NULL);
