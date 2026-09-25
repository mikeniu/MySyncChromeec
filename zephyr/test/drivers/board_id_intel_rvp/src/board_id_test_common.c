/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_id_test_common.h"
#include "cros_board_info.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_pwrseq.h>
#include <ap_power/ap_pwrseq_sm.h>
#include <drivers/rvp_board_id.h>

LOG_MODULE_REGISTER(rvp_model_id, LOG_LEVEL_DBG);

/* Pin assignments, must match boards/native_sim.overlay. */
#define BOARD_PIN_BASE 0
#define FAB_PIN_BASE 6
#define BOM_PIN_BASE 8

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, cbi_get_model_id, uint32_t *);
DEFINE_FAKE_VALUE_FUNC(int, cbi_set_model_id, uint32_t);

/*
 * The emulated AP power-sequence driver does not provide
 * get_ap_pwrseq_thread(), but the task shim references it. Supply a stub; the
 * task-mapping path that uses it is never exercised by these tests.
 */
k_tid_t get_ap_pwrseq_thread(void)
{
	return NULL;
}

#define RVP_GPIO_NODE DT_NODELABEL(rvp_id_gpio)

const struct device *const rvp_gpio_dev = DEVICE_DT_GET(RVP_GPIO_NODE);

/* Shared state used by the cbi_get_model_id custom fake. */
uint32_t cbi_model_id_value;

int cbi_get_model_id_match(uint32_t *id)
{
	*id = cbi_model_id_value;
	return EC_SUCCESS;
}

/*
 * The strap controller is deferred, so initialize it on demand for the suites
 * that read known strap values. This is a no-op once it is ready.
 */
void ensure_straps_ready(void)
{
	if (!device_is_ready(rvp_gpio_dev)) {
		zassert_ok(device_init(rvp_gpio_dev));
	}
}

void set_board_gpios(uint32_t board_id)
{
	for (int i = 0; i < BOARD_GPIOS_COUNT; i++) {
		zassert_ok(gpio_emul_input_set(rvp_gpio_dev, BOARD_PIN_BASE + i,
					       (board_id >> i) & 1));
	}
}

void set_fab_gpios(uint32_t raw_fab)
{
	for (int i = 0; i < FAB_GPIOS_COUNT; i++) {
		zassert_ok(gpio_emul_input_set(rvp_gpio_dev, FAB_PIN_BASE + i,
					       (raw_fab >> i) & 1));
	}
}

void set_bom_gpios(uint32_t bom_id)
{
	for (int i = 0; i < BOM_GPIOS_COUNT; i++) {
		zassert_ok(gpio_emul_input_set(rvp_gpio_dev, BOM_PIN_BASE + i,
					       (bom_id >> i) & 1));
	}
}

void send_ap_pwrseq_s5_exit(enum ap_pwrseq_state entry)
{
	STRUCT_SECTION_FOREACH(ap_pwrseq_state_cb, cb)
	{
		if (!cb->is_entry &&
		    (cb->states_bit_mask & BIT(AP_POWER_STATE_S5))) {
			cb->cb(NULL, entry, AP_POWER_STATE_S5);
		}
	}
}

void board_id_intel_rvp_reset(void)
{
	RESET_FAKE(cbi_get_model_id);
	RESET_FAKE(cbi_set_model_id);

	cbi_model_id_value = 0;

	/*
	 * The strap controller is deferred. Once a suite's setup (or the
	 * driver's S5 callback) has initialized it, reset its pins to a known
	 * state. Suites that exercise the not-ready paths never initialize it.
	 */
	if (device_is_ready(rvp_gpio_dev)) {
		for (int pin = 0; pin < 16; pin++) {
			zassert_ok(gpio_pin_configure(rvp_gpio_dev, pin,
						      GPIO_INPUT));
			zassert_ok(gpio_emul_input_set(rvp_gpio_dev, pin, 0));
		}
	}
}
