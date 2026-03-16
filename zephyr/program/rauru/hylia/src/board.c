/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "common.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "math_util.h"
#include "util.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/battery.h>

#define VOL_UP_KEY_ROW 0
#define VOL_UP_KEY_COL 11

#define STABLE_THRESHOLD 3

/* If the battery is bad, the battery reading will be more frequent. */
#define BAD_DELAY (500 * USEC_PER_MSEC)
#define GOOD_DELAY (30 * USEC_PER_SEC)

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_INF);

static void board_setup_init(void)
{
	set_vol_up_key(VOL_UP_KEY_ROW, VOL_UP_KEY_COL);
}
DECLARE_HOOK(HOOK_INIT, board_setup_init, HOOK_PRIO_PRE_DEFAULT);

static enum battery_present cached_batt_state = BP_NO;
static enum battery_present raw_state = BP_NO;
static int stable_count;

static void update_battery_state_cache(void);
DECLARE_DEFERRED(update_battery_state_cache);
/*
 * I2C read register to detect battery
 */
static void update_battery_state_cache(void)
{
	int state;
	int rv;
	enum battery_present current_state;

	/*
	 *  According to the battery manufacturer's reply:
	 *  To detect a bad battery, need to read the 0x00 register.
	 *  If the 12th bit(Permanently Failure) is 1, it means a bad battery.
	 */
	rv = sb_read(SB_MANUFACTURER_ACCESS, &state);

	/* If an i2c read exception occurs or PF Bit12 is 1, it is BT_NO. */
	if (rv || (state & BIT(12)))
		current_state = BP_NO;
	else
		current_state = BP_YES;

	/*
	 * Retry is added to avoid misjudgment.
	 */
	if (current_state == raw_state) {
		if (stable_count < STABLE_THRESHOLD)
			stable_count++;
	} else {
		raw_state = current_state;
		stable_count = 1;
	}

	if (stable_count >= STABLE_THRESHOLD)
		cached_batt_state = raw_state;

	hook_call_deferred(&update_battery_state_cache_data,
			   (cached_batt_state == BP_NO) ? BAD_DELAY :
							  GOOD_DELAY);
}
/*
 * I2C reads take 3.5ms. Battery_is_present is called continuously during
 * the boot process, which delays the DUT from loading powerd. To avoid
 * powerd delays, I2C reads are placed in update_battery_state_cache to
 * record register status.
 */
DECLARE_HOOK(HOOK_INIT, update_battery_state_cache, HOOK_PRIO_DEFAULT);

static enum battery_present battery_is_present_custom(void)
{
	if (gpio_get_level(GPIO_BATT_PRES_ODL)) {
		return BP_NO;
	}
	return cached_batt_state;
}

#ifdef CONFIG_BATTERY_PRESENT_CUSTOM
enum battery_present battery_is_present(void)
{
	return battery_is_present_custom();
}
#else
static enum battery_present battery_is_present_smart(void)
{
	int manufacture_date;

	if (sb_read(SB_MANUFACTURE_DATE, &manufacture_date)) {
		/* Require 2 consecutive failures before declaring the
		 * battery missing.
		 */
		k_busy_wait(1 * USEC_PER_MSEC);
		if (sb_read(SB_MANUFACTURE_DATE, &manufacture_date)) {
			return BP_NOT_SURE;
		}
	}

	return BP_YES;
}

enum battery_present battery_is_present(void)
{
	static const char *const batt_pres[] = {
		"NO",
		"YES",
		"NOT_SURE",
	};
	static atomic_t a_number_of_mismatch = ATOMIC_INIT(0);
	static atomic_t a_call_count = ATOMIC_INIT(0);
	static atomic_t a_last_state = ATOMIC_INIT(false);

	enum battery_present smart_result = battery_is_present_smart();
	enum battery_present custom_result = battery_is_present_custom();
	bool mismatch = smart_result != custom_result;
	bool last_state = atomic_set(&a_last_state, mismatch);
	atomic_val_t number_of_mismatch =
		atomic_add(&a_number_of_mismatch, mismatch) + mismatch;
	atomic_val_t call_count = atomic_inc(&a_call_count) + 1;

	if (call_count % 256 == 0 || last_state != mismatch) {
		LOG_INF("battery_is_present: called %ld times, "
			"mismatch %ld times, smart=%s %s custom=%s "
			"===============================",
			call_count, number_of_mismatch, batt_pres[smart_result],
			mismatch ? "!=" : "==", batt_pres[custom_result]);
	}

	return smart_result;
}
#endif
