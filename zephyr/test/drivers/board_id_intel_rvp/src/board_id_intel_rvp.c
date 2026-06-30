/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_pwrseq.h>
#include <ap_power/ap_pwrseq_sm.h>
#include <drivers/rvp_board_id.h>

LOG_MODULE_REGISTER(rvp_model_id, LOG_LEVEL_DBG);

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, cbi_get_model_id, uint32_t *);
FAKE_VALUE_FUNC(int, cbi_set_model_id, uint32_t);

/*
 * The emulated AP power-sequence driver does not provide
 * get_ap_pwrseq_thread(), but the task shim references it. Supply a stub; the
 * task-mapping path that uses it is never exercised by this test.
 */
k_tid_t get_ap_pwrseq_thread(void)
{
	return NULL;
}

void rvp_id_handler(void);
int board_get_version(void);

#define RVP_GPIO_NODE DT_NODELABEL(rvp_id_gpio)

static const struct device *const rvp_gpio_dev = DEVICE_DT_GET(RVP_GPIO_NODE);

/* Pin assignments, must match boards/native_sim.overlay. */
#define BOARD_PIN_BASE 0
#define FAB_PIN_BASE 6
#define BOM_PIN_BASE 8

#define BOARD_GPIOS_COUNT 6
#define FAB_GPIOS_COUNT 2
#define BOM_GPIOS_COUNT 3

#define BOARD_ID_MASK (BIT(BOARD_GPIOS_COUNT) - 1)
#define FAB_ID_SHIFT 8

/* Shared state used by the cbi_get_model_id custom fake. */
static uint32_t cbi_model_id_value;

static int cbi_get_model_id_match(uint32_t *id)
{
	*id = cbi_model_id_value;
	return EC_SUCCESS;
}

/*
 * The strap controller is deferred, so initialize it on demand for the tests
 * that read known strap values. This is a no-op once it is ready.
 */
static void ensure_straps_ready(void)
{
	if (!device_is_ready(rvp_gpio_dev)) {
		zassert_ok(device_init(rvp_gpio_dev));
	}
}

static void set_board_gpios(uint32_t board_id)
{
	ensure_straps_ready();
	for (int i = 0; i < BOARD_GPIOS_COUNT; i++) {
		zassert_ok(gpio_emul_input_set(rvp_gpio_dev, BOARD_PIN_BASE + i,
					       (board_id >> i) & 1));
	}
}

static void set_fab_gpios(uint32_t raw_fab)
{
	ensure_straps_ready();
	for (int i = 0; i < FAB_GPIOS_COUNT; i++) {
		zassert_ok(gpio_emul_input_set(rvp_gpio_dev, FAB_PIN_BASE + i,
					       (raw_fab >> i) & 1));
	}
}

static void set_bom_gpios(uint32_t bom_id)
{
	ensure_straps_ready();
	for (int i = 0; i < BOM_GPIOS_COUNT; i++) {
		zassert_ok(gpio_emul_input_set(rvp_gpio_dev, BOM_PIN_BASE + i,
					       (bom_id >> i) & 1));
	}
}

ZTEST(board_id_intel_rvp, test_board_get_version)
{
	/* CBI has no model id yet: board_get_version() reports an error. */
	cbi_get_model_id_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(board_get_version(), -1);

	/* CBI now provides a model id, it is cached and the board id returned.
	 */
	cbi_model_id_value = 0x22a;
	cbi_get_model_id_fake.custom_fake = cbi_get_model_id_match;
	zassert_equal(board_get_version(), 0x22a & BOARD_ID_MASK);
}

ZTEST(board_id_intel_rvp, test_board_id_config_not_ready)
{
	/* The GPIO controller is not ready: report a device error. */
	zassert_false(device_is_ready(rvp_gpio_dev));
	zassert_equal(get_rvp_id_config(BOARD_ID), -ENODEV);
}

ZTEST(board_id_intel_rvp, test_board_id_handler_not_ready)
{
	/*
	 * With the straps unavailable the handler cannot read the board id, so
	 * it bails out without writing CBI.
	 */
	zassert_false(device_is_ready(rvp_gpio_dev));

	rvp_id_handler();

	zassert_equal(cbi_set_model_id_fake.call_count, 0);
}

ZTEST(board_id_intel_rvp, test_get_rvp_id_config_board_id)
{
	set_board_gpios(0x2a);

	zassert_equal(get_rvp_id_config(BOARD_ID), 0x2a);
}

ZTEST(board_id_intel_rvp, test_get_rvp_id_config_fab_id)
{
	set_fab_gpios(0x1);

	zassert_equal(get_rvp_id_config(FAB_ID), 2);
}

ZTEST(board_id_intel_rvp, test_get_rvp_id_config_bom_id)
{
	set_bom_gpios(0x3);

	zassert_equal(get_rvp_id_config(BOM_ID), 3);
}

ZTEST(board_id_intel_rvp, test_get_rvp_id_config_invalid)
{
	ensure_straps_ready();
	zassert_equal(get_rvp_id_config((enum rvp_id_type)0xff), -1);
}

ZTEST(board_id_intel_rvp, test_rvp_id_handler_stores_in_cbi)
{
	set_board_gpios(0x2a);
	set_fab_gpios(0x1);

	/* CBI read fails, so the freshly computed model id is written back. */
	cbi_get_model_id_fake.return_val = EC_ERROR_UNKNOWN;

	rvp_id_handler();

	zassert_equal(cbi_set_model_id_fake.call_count, 1);
	zassert_equal(cbi_set_model_id_fake.arg0_val,
		      (2 << FAB_ID_SHIFT) | 0x2a);

	/*
	 * After the handler caches the model id, board_get_version() returns
	 * only the board id portion without consulting CBI again.
	 */
	zassert_equal(board_get_version(), 0x2a & BOARD_ID_MASK);
}

ZTEST(board_id_intel_rvp, test_rvp_id_handler_cbi_up_to_date)
{
	set_board_gpios(0x2a);
	set_fab_gpios(0x1);

	/* CBI already holds the same model id, nothing is written. */
	cbi_model_id_value = (2 << FAB_ID_SHIFT) | 0x2a;
	cbi_get_model_id_fake.custom_fake = cbi_get_model_id_match;

	rvp_id_handler();

	zassert_equal(cbi_set_model_id_fake.call_count, 0);
}

ZTEST(board_id_intel_rvp, test_rvp_id_handler_cbi_mismatch)
{
	set_board_gpios(0x2a);
	set_fab_gpios(0x1);

	/* CBI holds a stale model id, so it is refreshed. */
	cbi_model_id_value = 0x999;
	cbi_get_model_id_fake.custom_fake = cbi_get_model_id_match;

	rvp_id_handler();

	zassert_equal(cbi_set_model_id_fake.call_count, 1);
	zassert_equal(cbi_set_model_id_fake.arg0_val,
		      (2 << FAB_ID_SHIFT) | 0x2a);
}

ZTEST(board_id_intel_rvp, test_rvp_id_handler_cbi_set_fails)
{
	set_board_gpios(0x2a);
	set_fab_gpios(0x1);

	/* CBI read fails, so a write is attempted, but the write fails too. */
	cbi_get_model_id_fake.return_val = EC_ERROR_UNKNOWN;
	cbi_set_model_id_fake.return_val = EC_ERROR_UNKNOWN;

	rvp_id_handler();

	zassert_equal(cbi_set_model_id_fake.call_count, 1);
	zassert_equal(cbi_set_model_id_fake.arg0_val,
		      (2 << FAB_ID_SHIFT) | 0x2a);
}

static void send_ap_pwrseq_s5_exit(enum ap_pwrseq_state entry)
{
	STRUCT_SECTION_FOREACH(ap_pwrseq_state_cb, cb)
	{
		if (!cb->is_entry &&
		    (cb->states_bit_mask & BIT(AP_POWER_STATE_S5))) {
			cb->cb(NULL, entry, AP_POWER_STATE_S5);
		}
	}
}

ZTEST(board_id_intel_rvp, test_callback_inits_deferred_gpios)
{
	zassert_false(device_is_ready(rvp_gpio_dev));

	/* Avoid an uninitialized CBI read inside the handler. */
	cbi_get_model_id_fake.return_val = EC_ERROR_UNKNOWN;

	/* Exiting S5 into a higher-power state (S3 > S5) runs the callback. */
	send_ap_pwrseq_s5_exit(AP_POWER_STATE_S3);

	/* The callback called device_init() on the strap controller. */
	zassert_true(device_is_ready(rvp_gpio_dev));
}

ZTEST(board_id_intel_rvp, test_s5_exit_callback_runs_handler)
{
	set_board_gpios(0x2a);
	set_fab_gpios(0x1);

	/* CBI has no model id yet, so the handler writes the computed one. */
	cbi_get_model_id_fake.return_val = EC_ERROR_UNKNOWN;

	/*
	 * Exiting S5 into a higher-power state (S3 > S5) triggers the driver
	 * callback, which initializes the strap GPIOs and runs the handler.
	 */
	send_ap_pwrseq_s5_exit(AP_POWER_STATE_S3);

	zassert_equal(cbi_set_model_id_fake.call_count, 1);
	zassert_equal(cbi_set_model_id_fake.arg0_val,
		      (2 << FAB_ID_SHIFT) | 0x2a);
}

static void board_id_intel_rvp_before(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(cbi_get_model_id);
	RESET_FAKE(cbi_set_model_id);

	cbi_model_id_value = 0;

	/*
	 * The strap controller is deferred. Once a test (or the driver's S5
	 * callback) has initialized it, reset its pins to a known state. Tests
	 * that rely on the not-ready state run before any initialization.
	 */
	if (device_is_ready(rvp_gpio_dev)) {
		for (int pin = 0; pin < 16; pin++) {
			zassert_ok(gpio_pin_configure(rvp_gpio_dev, pin,
						      GPIO_INPUT));
			zassert_ok(gpio_emul_input_set(rvp_gpio_dev, pin, 0));
		}
	}
}

ZTEST_SUITE(board_id_intel_rvp, NULL, NULL, board_id_intel_rvp_before, NULL,
	    NULL);
