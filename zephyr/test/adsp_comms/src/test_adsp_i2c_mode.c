/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adsp_comms.h"
#include "battery.h"
#include "charge_manager.h"
#include "chipset.h"
#include "hooks.h"
#include "i2c.h"
#include "stubs.h"

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

extern struct i2c_target_config target_cfg;
void board_chipset_startup_i2c_target(void);
void board_chipset_shutdown_complete_i2c_controller(void);

/*
 * Note on I2C target mode assertions:
 * In the emulated I2C driver (i2c_emul.c), i2c_target_unregister(dev, cfg)
 * returns 0 when cfg is currently registered as the active target, and -EINVAL
 * when it is not registered (i.e. controller mode is active). This is used in
 * the tests below to verify target vs controller state transitions.
 */

static void test_before(void *fixture)
{
	stubs_reset();
}

/* Test I2C target read callbacks return default 0xFF response */
ZTEST_USER(adsp_i2c_mode, test_target_callbacks_read)
{
	uint8_t val = 0;

	zassert_not_null(target_cfg.callbacks);
	zassert_not_null(target_cfg.callbacks->read_requested);
	zassert_not_null(target_cfg.callbacks->read_processed);

	zassert_equal(target_cfg.callbacks->read_requested(&target_cfg, &val),
		      0);
	zassert_equal(val, 0xFF);

	val = 0;
	zassert_equal(target_cfg.callbacks->read_processed(&target_cfg, &val),
		      0);
	zassert_equal(val, 0xFF);
}

/* Test I2C target write byte accumulation and buffer overflow handling */
ZTEST_USER(adsp_i2c_mode, test_target_callbacks_write_and_overflow)
{
	zassert_not_null(target_cfg.callbacks);
	zassert_not_null(target_cfg.callbacks->write_requested);
	zassert_not_null(target_cfg.callbacks->write_received);

	zassert_equal(target_cfg.callbacks->write_requested(&target_cfg), 0);

	/* 4 valid bytes */
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0x01),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0x02),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0x03),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0x04),
		      0);

	/* 5th byte should overflow and return -ENOMEM */
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0x05),
		      -ENOMEM);
}

/* Test I2C target stop callback processing for empty, partial, and full packets
 */
ZTEST_USER(adsp_i2c_mode, test_target_callbacks_stop)
{
	zassert_not_null(target_cfg.callbacks->stop);

	/* Stop with 0 bytes */
	zassert_equal(target_cfg.callbacks->write_requested(&target_cfg), 0);
	zassert_equal(target_cfg.callbacks->stop(&target_cfg), 0);

	/* Stop with incomplete packet (e.g. 2 bytes) */
	zassert_equal(target_cfg.callbacks->write_requested(&target_cfg), 0);
	zassert_equal(target_cfg.callbacks->write_received(
			      &target_cfg, ADSP_OEM_CUSTOM_REG_MAGIC),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(
			      &target_cfg, ADSP_FEATURE_OEM_CUSTOM),
		      0);
	zassert_equal(target_cfg.callbacks->stop(&target_cfg), 0);

	/* Stop with full 4-byte packet */
	zassert_equal(target_cfg.callbacks->write_requested(&target_cfg), 0);
	zassert_equal(target_cfg.callbacks->write_received(
			      &target_cfg, ADSP_OEM_CUSTOM_REG_MAGIC),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(
			      &target_cfg, ADSP_FEATURE_OEM_CUSTOM),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(
			      &target_cfg, ADSP_OEM_CUSTOM_MAGIC_VAL),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0x00),
		      0);
	zassert_equal(target_cfg.callbacks->stop(&target_cfg), 0);

	k_msleep(10);
}

/* Test ADSP message queue full condition when incoming packets exceed capacity
 */
ZTEST(adsp_i2c_mode, test_target_callbacks_queue_full)
{
	/* Fill message queue to trigger queue full branch */
	for (int i = 0; i < CONFIG_ADSP_COMMS_MSGQ_SIZE + 5; i++) {
		zassert_equal(
			target_cfg.callbacks->write_requested(&target_cfg), 0);
		zassert_equal(target_cfg.callbacks->write_received(
				      &target_cfg, ADSP_OEM_CUSTOM_REG_MAGIC),
			      0);
		zassert_equal(target_cfg.callbacks->write_received(
				      &target_cfg, ADSP_FEATURE_OEM_CUSTOM),
			      0);
		zassert_equal(target_cfg.callbacks->write_received(
				      &target_cfg, ADSP_OEM_CUSTOM_MAGIC_VAL),
			      0);
		zassert_equal(target_cfg.callbacks->write_received(&target_cfg,
								   0x00),
			      0);
		zassert_equal(target_cfg.callbacks->stop(&target_cfg), 0);
	}

	k_msleep(20);
}

/* Test processing of messages with unhandled feature IDs and register addresses
 */
ZTEST_USER(adsp_i2c_mode, test_unhandled_callback)
{
	zassert_equal(charge_manager_get_active_charge_port(),
		      CHARGE_PORT_NONE);
	zassert_equal(battery_get_fake_soc(), -1);

	/* Send message with unhandled fid and addr */
	zassert_equal(target_cfg.callbacks->write_requested(&target_cfg), 0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0xAA),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0xBB),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0x34),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0x12),
		      0);
	zassert_equal(target_cfg.callbacks->stop(&target_cfg), 0);

	k_msleep(10);

	/* Verify state is unaffected by unhandled callback */
	zassert_equal(charge_manager_get_active_charge_port(),
		      CHARGE_PORT_NONE);
	zassert_equal(battery_get_fake_soc(), -1);
}

/* Test switching ADSP I2C bus to target mode on chipset startup */
ZTEST_USER(adsp_i2c_mode, test_board_chipset_startup_i2c_target)
{
	const struct device *i2c_dev = i2c_get_device_for_port(I2C_PORT_ADSP);

	zassert_not_null(i2c_dev);

	/* Ensure starting from controller mode */
	board_chipset_shutdown_complete_i2c_controller();
	zassert_not_equal(i2c_target_unregister(i2c_dev, &target_cfg), 0);

	/* Success path: switches to target mode */
	board_chipset_startup_i2c_target();
	zassert_ok(i2c_target_unregister(i2c_dev, &target_cfg));

	/* Re-register target and test error path / redundant call */
	board_chipset_startup_i2c_target();
	board_chipset_startup_i2c_target();

	/* Revert to controller mode */
	board_chipset_shutdown_complete_i2c_controller();
	zassert_not_equal(i2c_target_unregister(i2c_dev, &target_cfg), 0);

	/* Hook notification */
	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_ok(i2c_target_unregister(i2c_dev, &target_cfg));

	board_chipset_startup_i2c_target();
	hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);
	zassert_not_equal(i2c_target_unregister(i2c_dev, &target_cfg), 0);
}

/* Test initialization of ADSP I2C bus mode on HOOK_INIT for ON and OFF states
 */
ZTEST_USER(adsp_i2c_mode, test_adsp_i2c_init)
{
	const struct device *i2c_dev = i2c_get_device_for_port(I2C_PORT_ADSP);

	zassert_not_null(i2c_dev);

	/* Ensure starting from controller mode */
	board_chipset_shutdown_complete_i2c_controller();

	/* When chipset is in ON state, HOOK_INIT configures as target */
	chipset_in_state_fake.return_val = 1;
	hook_notify(HOOK_INIT);
	zassert_equal(chipset_in_state_fake.call_count, 1);
	zassert_equal(chipset_in_state_fake.arg0_val, CHIPSET_STATE_ON);
	/* Target is now registered, unregistering directly should succeed */
	zassert_ok(i2c_target_unregister(i2c_dev, &target_cfg));

	/*
	 * When chipset is not in ON state (adsp_i2c_init specifically queries
	 * CHIPSET_STATE_ON, so verify the query and that target mode is not
	 * enabled)
	 */
	RESET_FAKE(chipset_in_state);
	chipset_in_state_fake.return_val = 0;
	hook_notify(HOOK_INIT);
	zassert_equal(chipset_in_state_fake.call_count, 1);
	zassert_equal(chipset_in_state_fake.arg0_val, CHIPSET_STATE_ON);
	/* Target was not registered, unregistering should fail */
	zassert_not_equal(i2c_target_unregister(i2c_dev, &target_cfg), 0);
}

/* Test switching ADSP I2C bus back to controller mode on chipset shutdown */
ZTEST_USER(adsp_i2c_mode, test_board_chipset_shutdown_complete_i2c_controller)
{
	const struct device *i2c_dev = i2c_get_device_for_port(I2C_PORT_ADSP);

	zassert_not_null(i2c_dev);

	/* Success path: register first then shutdown */
	board_chipset_startup_i2c_target();
	board_chipset_shutdown_complete_i2c_controller();
	zassert_not_equal(i2c_target_unregister(i2c_dev, &target_cfg), 0);

	/* Error path: unregister when not registered fails gracefully */
	board_chipset_shutdown_complete_i2c_controller();
	zassert_not_equal(i2c_target_unregister(i2c_dev, &target_cfg), 0);
}

ZTEST_SUITE(adsp_i2c_mode, NULL, NULL, test_before, NULL, NULL);
