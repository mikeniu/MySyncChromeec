/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for panic.
 */

#include "common.h"
#include "ec_tasks.h"
#include "panic.h"
#include "system.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

int panic_data_init(void);

struct panic_test_fixture {
	struct panic_data saved_pdata;
};

static void *panic_test_setup(void)
{
	static struct panic_test_fixture panic_fixture = { 0 };

	return &panic_fixture;
}

static void panic_before(void *state)
{
	struct panic_test_fixture *fixture = state;
	struct panic_data *pdata = get_panic_data_write();

	ARG_UNUSED(state);
	system_clear_reset_flags(-1);

	fixture->saved_pdata = *pdata;
}

static void panic_after(void *state)
{
	struct panic_test_fixture *fixture = state;
	struct panic_data *pdata = get_panic_data_write();

	ARG_UNUSED(state);

	*pdata = fixture->saved_pdata;
}

/**
 * @brief Test Suite: Verifies panic functionality.
 */
ZTEST_SUITE(panic, drivers_predicate_post_main, panic_test_setup, panic_before,
	    panic_after, NULL);

/**
 * @brief TestPurpose: Verify panic set/get reason.
 *
 * @details
 * Validate panic set/get reason.
 *
 * Expected Results
 *  - Success
 */
ZTEST(panic, test_panic_reason)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;
	struct panic_data *pdata = panic_get_data();

	zassert_is_null(pdata, NULL);
	panic_set_reason(PANIC_SW_WATCHDOG, 0, 0);

	panic_get_reason(&reason, &info, &exception);

	zassert_equal(PANIC_SW_WATCHDOG, reason);
	zassert_equal(0, info);
	zassert_equal(0, exception);

	pdata = panic_get_data();
	zassert_not_null(pdata, NULL);
	zassert_equal(pdata->struct_version, 2);
	zassert_equal(pdata->magic, PANIC_DATA_MAGIC);
	zassert_equal(pdata->struct_size, CONFIG_PANIC_DATA_SIZE);

	panic_data_print(pdata);
}

ZTEST(panic, test_panic_data_start_bad_magic)
{
	struct panic_data *pdata = get_panic_data_write();

	pdata->magic = PANIC_DATA_MAGIC + 1;
	zassert_equal(0, get_panic_data_start(), NULL);
}

ZTEST(panic, test_get_panic_data_start)
{
	struct panic_data *pdata = get_panic_data_write();

	pdata->magic = PANIC_DATA_MAGIC;
	zassert_equal((uintptr_t)pdata, get_panic_data_start(), NULL);
}

ZTEST(panic, test_panic_data_init__watch_dog_panic)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	/* Watchdog reset should result in any existing panic data being
	 * overwritten (if in RW)
	 */
	panic_set_reason(PANIC_SW_DIV_ZERO, 0x12, 0x34);

	/* Clear all reset flags and set them arbitrarily */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);

	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		zassert_equal(reason, PANIC_SW_WATCHDOG_HARD);
		zassert_equal(info, 0);
		zassert_equal(exception, 0);
	} else {
		/* In RO, existing panic reason should remain */
		zassert_equal(reason, PANIC_SW_DIV_ZERO);
	}
}

ZTEST(panic, test_panic_data_init__watch_dog_warn_panic)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	/* Panic reason PANIC_SW_WATCHDOG_WARN should be switched
	 * to PANIC_SW_WATCHDOG after a watchdog reset (if in RW).
	 * Info and exception should be preserved.
	 */
	panic_set_reason(PANIC_SW_WATCHDOG_WARN, 0x12, 0x34);

	/* Clear all reset flags and set them arbitrarily */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);

	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		zassert_equal(reason, PANIC_SW_WATCHDOG);
		zassert_equal(info, 0x12);
		zassert_equal(exception, 0x34);
	} else {
		/* In RO, existing reason remains */
		zassert_equal(reason, PANIC_SW_WATCHDOG_WARN);
	}
}

ZTEST(panic, test_panic_data_init__watch_dog_panic_already_initialized)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	/* Watchdog reset should not overwrite panic info if already filled
	 * in with watchdog panic info that HAS NOT been read by host
	 */
	panic_set_reason(PANIC_SW_WATCHDOG, 0x12, 0x34);

	/* Clear all reset flags and set them arbitrarily */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);
	zassert_equal(reason, PANIC_SW_WATCHDOG);
	zassert_equal(info, 0x12);
	zassert_equal(exception, 0x34);
}

ZTEST(panic, test_panic_data_init__watch_dog_hard_panic_already_initialized)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	/* Watchdog reset should not overwrite panic info if already filled
	 * in with watchdog hard panic info that HAS NOT been read by host
	 */
	panic_set_reason(PANIC_SW_WATCHDOG_HARD, 0x12, 0x34);

	/* Clear all reset flags and set them arbitrarily */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);
	zassert_equal(reason, PANIC_SW_WATCHDOG_HARD);
	zassert_equal(info, 0x12);
	zassert_equal(exception, 0x34);
}

ZTEST(panic, test_panic_data_init__watch_dog_panic_already_read)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;
	struct panic_data *pdata;

	/* Watchdog reset should overwrite panic info if already filled
	 * in with watchdog panic info that HAS been read by host (if in RW)
	 */
	panic_set_reason(PANIC_SW_WATCHDOG, 0x12, 0x34);
	pdata = get_panic_data_write();
	pdata->flags |= PANIC_DATA_FLAG_OLD_HOSTCMD;

	/* Clear all reset flags and set them arbitrarily */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);

	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		zassert_equal(reason, PANIC_SW_WATCHDOG_HARD);
		zassert_equal(info, 0);
		zassert_equal(exception, 0);
	} else {
		/* In RO, existing info remains even if already read */
		zassert_equal(info, 0x12);
	}
}

ZTEST(panic, test_panic_data_init__no_watchdog_reset_flag)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	panic_set_reason(PANIC_SW_DIV_ZERO, 0x12, 0x34);
	system_set_reset_flags(EC_RESET_FLAG_POWER_ON);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);

	zassert_equal(reason, PANIC_SW_DIV_ZERO);
	zassert_equal(info, 0x12);
	zassert_equal(exception, 0x34);
}

ZTEST(panic, test_panic_data_init__watch_dog_hard_panic_already_read)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;
	struct panic_data *pdata;

	panic_set_reason(PANIC_SW_WATCHDOG_HARD, 0x12, 0x34);
	pdata = get_panic_data_write();
	pdata->flags |= PANIC_DATA_FLAG_OLD_HOSTCMD;

	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);

	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		zassert_equal(reason, PANIC_SW_WATCHDOG_HARD);
		zassert_equal(info, 0);
		zassert_equal(exception, 0);
	} else {
		zassert_equal(info, 0x12);
	}
}

ZTEST(panic, test_panic_data_init__invalid_sw_reason)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	panic_set_reason(0, 0x12, 0x34);
	system_set_reset_flags(EC_RESET_FLAG_POWER_ON);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);

	zassert_equal(reason, 0);
	zassert_equal(info, 0x12);
	zassert_equal(exception, 0x34);
}

ZTEST(panic, test_panic_data_init__old_panic_no_watchdog)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;
	struct panic_data *pdata;

	panic_set_reason(PANIC_SW_DIV_ZERO, 0x12, 0x34);
	pdata = get_panic_data_write();
	pdata->flags |= PANIC_DATA_FLAG_OLD_HOSTCMD;

	system_set_reset_flags(EC_RESET_FLAG_POWER_ON);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);

	zassert_equal(reason, PANIC_SW_DIV_ZERO);
	zassert_equal(info, 0x12);
	zassert_equal(exception, 0x34);
}

ZTEST(panic, test_panic_data_init__invalid_sw_reason_watchdog)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	panic_set_reason(0, 0x12, 0x34);
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);

	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		zassert_equal(reason, PANIC_SW_WATCHDOG_HARD);
		zassert_equal(info, 0);
		zassert_equal(exception, 0);
	} else {
		zassert_equal(reason, 0);
		zassert_equal(info, 0x12);
		zassert_equal(exception, 0x34);
	}
}
