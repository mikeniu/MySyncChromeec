/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adsp_comms.h"
#include "chipset.h"
#include "hooks.h"
#include "stubs.h"

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

extern struct i2c_target_config target_cfg;
void board_chipset_startup_i2c_target(void);
void board_chipset_shutdown_complete_i2c_controller(void);

static void test_before(void *fixture)
{
	stubs_reset();
}

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

ZTEST_USER(adsp_i2c_mode, test_target_callbacks_stop)
{
	zassert_not_null(target_cfg.callbacks->stop);

	/* Stop with 0 bytes */
	zassert_equal(target_cfg.callbacks->write_requested(&target_cfg), 0);
	zassert_equal(target_cfg.callbacks->stop(&target_cfg), 0);

	/* Stop with incomplete packet (e.g. 2 bytes) */
	zassert_equal(target_cfg.callbacks->write_requested(&target_cfg), 0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0x01),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0x02),
		      0);
	zassert_equal(target_cfg.callbacks->stop(&target_cfg), 0);

	/* Stop with full 4-byte packet */
	zassert_equal(target_cfg.callbacks->write_requested(&target_cfg), 0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0x01),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0x07),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0xec),
		      0);
	zassert_equal(target_cfg.callbacks->write_received(&target_cfg, 0x00),
		      0);
	zassert_equal(target_cfg.callbacks->stop(&target_cfg), 0);

	k_msleep(10);
}

ZTEST(adsp_i2c_mode, test_target_callbacks_queue_full)
{
	/* Fill message queue to trigger queue full branch */
	for (int i = 0; i < CONFIG_ADSP_COMMS_MSGQ_SIZE + 5; i++) {
		target_cfg.callbacks->write_requested(&target_cfg);
		target_cfg.callbacks->write_received(&target_cfg, 0x01);
		target_cfg.callbacks->write_received(&target_cfg, 0x07);
		target_cfg.callbacks->write_received(&target_cfg, 0xec);
		target_cfg.callbacks->write_received(&target_cfg, 0x00);
		target_cfg.callbacks->stop(&target_cfg);
	}

	k_msleep(20);
}

ZTEST_USER(adsp_i2c_mode, test_unhandled_callback)
{
	/* Send message with unhandled fid and addr */
	target_cfg.callbacks->write_requested(&target_cfg);
	target_cfg.callbacks->write_received(&target_cfg, 0xAA);
	target_cfg.callbacks->write_received(&target_cfg, 0xBB);
	target_cfg.callbacks->write_received(&target_cfg, 0x34);
	target_cfg.callbacks->write_received(&target_cfg, 0x12);
	target_cfg.callbacks->stop(&target_cfg);

	k_msleep(10);
}

ZTEST_USER(adsp_i2c_mode, test_board_chipset_startup_i2c_target)
{
	/* Ensure starting from controller mode */
	board_chipset_shutdown_complete_i2c_controller();

	/* Success path: switches to target mode */
	board_chipset_startup_i2c_target();

	/* Error path: registering again fails because already registered */
	board_chipset_startup_i2c_target();

	/* Revert to controller mode */
	board_chipset_shutdown_complete_i2c_controller();

	/* Hook notification */
	hook_notify(HOOK_CHIPSET_STARTUP);
	hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);
}

ZTEST_USER(adsp_i2c_mode, test_adsp_i2c_init)
{
	/* When chipset is ON */
	chipset_in_state_fake.return_val = 1;
	hook_notify(HOOK_INIT);
	board_chipset_shutdown_complete_i2c_controller();

	/* When chipset is OFF */
	chipset_in_state_fake.return_val = 0;
	hook_notify(HOOK_INIT);
}

ZTEST_USER(adsp_i2c_mode, test_board_chipset_shutdown_complete_i2c_controller)
{
	/* Success path: register first then shutdown */
	board_chipset_startup_i2c_target();
	board_chipset_shutdown_complete_i2c_controller();

	/* Error path: unregister when not registered fails */
	board_chipset_shutdown_complete_i2c_controller();
}

ZTEST_SUITE(adsp_i2c_mode, NULL, NULL, test_before, NULL, NULL);
