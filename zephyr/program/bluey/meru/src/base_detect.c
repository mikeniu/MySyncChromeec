/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "ap_power/ap_power.h"
#include "base_state.h"
#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "host_command.h"
#include "tablet_mode.h"
#include "util.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

K_MUTEX_DEFINE(modify_base_detection_mutex);

#define BASE_DETECT_INTERVAL (30 * USEC_PER_MSEC)
#define BASE_DETECT_EN_DEBOUNCE_US (300 * USEC_PER_MSEC)
#define BASE_DETECT_DIS_DEBOUNCE_US (0 * USEC_PER_MSEC)

#define BASE_ATTACH_TH_NORMAL_MV 1200
#define BASE_DETACH_TH_NORMAL_MV 1500

#define BASE_ATTACH_TH_LID_MV 700
#define BASE_DETACH_TH_LID_MV 780

static bool attached;
static bool debouncing;

static void base_update(void);
DECLARE_DEFERRED(base_update);

static void base_get_thresholds(int *attach_th, int *detach_th)
{
	if (!gpio_pin_get_dt(
		    GPIO_DT_FROM_NODELABEL(gpio_ec_keyboard_det_cntl))) {
		*attach_th = BASE_ATTACH_TH_LID_MV;
		*detach_th = BASE_DETACH_TH_LID_MV;
	} else {
		*attach_th = BASE_ATTACH_TH_NORMAL_MV;
		*detach_th = BASE_DETACH_TH_NORMAL_MV;
	}
}

static void base_update(void)
{
	base_set_state(attached);
	tablet_set_mode(!attached, TABLET_TRIGGER_BASE);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp3300_base_x),
			attached);
}

static void base_detect_tick(void);
DECLARE_DEFERRED(base_detect_tick);

static void base_detect_tick(void)
{
	int attach_th;
	int detach_th;
	int next_us = BASE_DETECT_INTERVAL;
	int mv = adc_read_channel(ADC_BASE_DET);

	base_get_thresholds(&attach_th, &detach_th);

	if ((mv > detach_th) && base_get_state()) {
		if (!debouncing) {
			debouncing = true;
			next_us = BASE_DETECT_DIS_DEBOUNCE_US;
		} else {
			debouncing = false;
			attached = false;
			CPRINTS("Base detached (adc=%d mV)", mv);
			base_update();
		}
	} else if (mv <= attach_th && !base_get_state()) {
		if (!debouncing) {
			debouncing = true;
			next_us = BASE_DETECT_EN_DEBOUNCE_US;
		} else {
			debouncing = false;
			attached = true;
			CPRINTS("Base attached (adc=%d mV)", mv);
			base_update();
		}
	} else {
		debouncing = false;
	}

	hook_call_deferred(&base_detect_tick_data, next_us);
}

void base_detect_control(void)
{
	bool lid_open =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_lid_open_z5_ec));
	bool lid_360 = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_lid_360_l));
	bool enable = !lid_open || !lid_360;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_keyboard_det_cntl),
			enable);
	hook_call_deferred(&base_detect_tick_data, 0);
}
DECLARE_HOOK(HOOK_LID_CHANGE, base_detect_control, HOOK_PRIO_DEFAULT);

void tablet_interrupt(enum gpio_signal signal)
{
	base_detect_control();
}

static void base_detect_enable(bool enable)
{
	if (enable) {
		hook_call_deferred(&base_detect_tick_data,
				   BASE_DETECT_INTERVAL);
	} else {
		hook_call_deferred(&base_detect_tick_data, -1);
		attached = false;
		hook_call_deferred(&base_update_data, 0);
	}
}

static void base_startup_hook(struct ap_power_ev_callback *cb,
			      struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_STARTUP:
		base_detect_enable(true);
		break;
	case AP_POWER_SHUTDOWN:
		base_detect_enable(false);
		break;
	default:
		return;
	}
}
AP_POWER_EVENT_CALLBACK_DEFINE(base_startup_hook, AP_POWER_STARTUP,
			       AP_POWER_SHUTDOWN);

static int base_init(void)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		base_detect_enable(true);
	}

	return 0;
}

SYS_INIT(base_init, APPLICATION, 1);

void base_init_setting(void)
{
	int mv;
	int attach_th;
	int detach_th;

	base_detect_control();
	mv = adc_read_channel(ADC_BASE_DET);
	base_get_thresholds(&attach_th, &detach_th);

	if (mv <= attach_th) {
		attached = true;
	} else if (mv > detach_th) {
		attached = false;
	}

	CPRINTS("Base init: adc=%d mV, attach_th=%d mV, "
		"detach_th=%d mV, state=%s",
		mv, attach_th, detach_th, attached ? "attached" : "detached");

	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_360));
	hook_call_deferred(&base_update_data, 0);
	base_detect_enable(true);
}
DECLARE_HOOK(HOOK_INIT, base_init_setting, HOOK_PRIO_DEFAULT);

void base_force_state(enum ec_set_base_state_cmd state)
{
	k_mutex_lock(&modify_base_detection_mutex, K_FOREVER);
	switch (state) {
	case EC_SET_BASE_STATE_ATTACH:
		base_detect_enable(false);
		attached = true;
		base_update();
		break;
	case EC_SET_BASE_STATE_DETACH:
		base_detect_enable(false);
		attached = false;
		base_update();
		break;
	case EC_SET_BASE_STATE_RESET:
		base_detect_enable(true);
		break;
	}
	k_mutex_unlock(&modify_base_detection_mutex);
}
