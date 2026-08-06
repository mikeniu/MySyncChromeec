/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "power_button.h"
#include "power_led.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(power_button_interrupt, LOG_LEVEL_INF);

static void enable_powerled_interrupt(void)
{
	/* 启用 int_s3_power_monitor 中断 */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_power_button_new));
}
/* 注册初始化钩子 */
DECLARE_HOOK(HOOK_INIT, enable_powerled_interrupt, HOOK_PRIO_DEFAULT);

/*
 * Debounce delay for power button (30ms, matches common/power_button.c).
 */
#define POWER_BTN_DEBOUNCE_US (30 * 1000)

/*
 * Deferred work item for debounced button handling.
 * This avoids processing bounce glitches during mechanical press.
 */
static void power_btn_debounce_work_handler(struct k_work *work);
K_WORK_DEFINE(power_btn_debounce_work_item, power_btn_debounce_work_handler);

/*
 * Debounce timer — schedules the deferred work after debounce delay.
 */
static void power_btn_debounce_timer_callback(struct k_timer *timer);
K_TIMER_DEFINE(power_btn_debounce_timer, power_btn_debounce_timer_callback,
	       NULL);

static void power_btn_debounce_timer_callback(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	k_work_submit(&power_btn_debounce_work_item);
}

/*
 * Debounced power button handler.
 * Only triggers after the signal has been stable for POWER_BTN_DEBOUNCE_US.
 * Cycle logic: each valid press advances the LED state.
 */
static void power_btn_debounce_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Read the raw GPIO level */
	int val = gpio_get_level(GPIO_POWER_BUTTON_L);
	if (val < 0) {
		printk("Failed to read power button GPIO: %d", val);
		return;
	}

	/*
	 * Only act on press (active-low: val == 0 means pressed).
	 * Release events are ignored — cycle is edge-triggered.
	 */
	if (val != 0)
		return;

	/* Toggle LED blink state */
	led_set_power_blink();
	printk("Power button pressed → LED state change!!\n");
}

/*
 * Interrupt handler for power_new_button_interrupt.
 * Registered via gpio-interrupts DTS node (int_power_button_new).
 *
 * GPIO is active-low:
 *   value == 0 (low)  → button pressed
 *   value == 1 (high) → button released
 *
 * On every edge, we schedule a debounce timer. After the timer fires
 * (after stable delay), the deferred work reads the level and toggles
 * the LED blink only if it was a genuine press (not bounce).
 */
void power_new_button_interrupt(enum gpio_signal signal)
{
	printk("Power button pressed → LED state change!!\n");
	ARG_UNUSED(signal);
	/*
	 * (Re)start the debounce timer on every edge.
	 * This effectively extends the delay until the signal stabilizes.
	 */
	k_timer_start(&power_btn_debounce_timer, K_USEC(POWER_BTN_DEBOUNCE_US),
		      K_NO_WAIT);
}
