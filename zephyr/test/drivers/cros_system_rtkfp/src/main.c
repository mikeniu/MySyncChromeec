/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Tests for the REALTEK RTS5817 cros-system driver (cros_system_rtkfp.c).
 *
 * On the host the RTS5817 registers do not exist, so the driver backs
 * them with RAM (see cros_system_rtkfp.c).  The tests drive the reset
 * cause state machine through that backing storage and call the exported
 * pure helpers directly with synthetic register values.
 */

#include "drivers/cros_system.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

/* Interfaces exported by the driver under test. */
extern uint32_t cros_system_rtkfp_test_r_al_dummy0;
extern uint32_t cros_system_rtkfp_test_r_sys_sts;
int cros_system_rtkfp_init(void);
uint8_t cros_system_rtkfp_get_chip_version(uint32_t sys_sts);
int cros_system_rtkfp_determine_reset_cause(uint32_t reg_dummy0);

/* Bit in R_AL_DUMMY0 that records that the EC has booted once. */
#define TEST_SYSTEM_RUN_FLAG BIT(16)

ZTEST(cros_system_rtkfp, test_chip_identity)
{
	zassert_str_equal(cros_system_chip_vendor(), "rtk");
	zassert_str_equal(cros_system_chip_name(), "rts5817");
}

ZTEST(cros_system_rtkfp, test_get_chip_version)
{
	zassert_equal(cros_system_rtkfp_get_chip_version(0x00000000), 0);
	zassert_equal(cros_system_rtkfp_get_chip_version(0x00420000), 66);
	zassert_equal(cros_system_rtkfp_get_chip_version(0x00ff0000), 255);
	/* Only bits [23:16] hold the version, other fields are ignored. */
	zassert_equal(cros_system_rtkfp_get_chip_version(0xab3400cd), 0x34);
}

ZTEST(cros_system_rtkfp, test_determine_reset_cause)
{
	zassert_equal(cros_system_rtkfp_determine_reset_cause(0), POWERUP);
	/* Unrelated flags must not be mistaken for a watchdog reset. */
	zassert_equal(cros_system_rtkfp_determine_reset_cause(BIT(0)),
		      POWERUP);
	zassert_equal(
		cros_system_rtkfp_determine_reset_cause(TEST_SYSTEM_RUN_FLAG),
		WATCHDOG_RST);
}

ZTEST(cros_system_rtkfp, test_init_powerup_sets_run_flag)
{
	cros_system_rtkfp_test_r_al_dummy0 = 0;

	zassert_ok(cros_system_rtkfp_init());

	zassert_equal(cros_system_get_reset_cause(), POWERUP);
	zassert_true(cros_system_rtkfp_test_r_al_dummy0 &
		     TEST_SYSTEM_RUN_FLAG);
}

ZTEST(cros_system_rtkfp, test_init_detects_watchdog_reset)
{
	cros_system_rtkfp_test_r_al_dummy0 = TEST_SYSTEM_RUN_FLAG;

	zassert_ok(cros_system_rtkfp_init());

	zassert_equal(cros_system_get_reset_cause(), WATCHDOG_RST);
	/* The run flag must survive the watchdog reset detection. */
	zassert_true(cros_system_rtkfp_test_r_al_dummy0 &
		     TEST_SYSTEM_RUN_FLAG);
}

ZTEST(cros_system_rtkfp, test_chip_revision)
{
	cros_system_rtkfp_test_r_sys_sts = 0x00050000;

	zassert_str_equal(cros_system_chip_revision(), "5");
	/* The revision never changes, so it is evaluated only once. */
	zassert_str_equal(cros_system_chip_revision(), "5");
}

ZTEST(cros_system_rtkfp, test_hibernate_is_a_noop)
{
	zassert_ok(cros_system_hibernate(1, 0));
}

ZTEST_SUITE(cros_system_rtkfp, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
