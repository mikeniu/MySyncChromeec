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
void copy_esf_to_panic_data(const struct arch_esf *esf,
			    struct panic_data *pdata);
void flush_panic_data(const struct panic_data *pdata);

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

#define DEFAULT_PANIC_FLAGS                                         \
	(IS_ENABLED(CONFIG_CROS_EC_RW) ? PANIC_DATA_FLAG_RW_IMAGE : \
					 PANIC_DATA_FLAG_RO_IMAGE)

#if defined(CONFIG_ARM)
#define PANIC_ARCH PANIC_ARCH_CORTEX_M
#elif defined(CONFIG_RISCV)
#define PANIC_ARCH PANIC_ARCH_RISCV_RV32I
#elif defined(CONFIG_X86)
#define PANIC_ARCH PANIC_ARCH_X86
#else
#define PANIC_ARCH PANIC_ARCH_UNSUPPORTED
#endif

static void init_expected_panic_data(struct panic_data *expected,
				     uint32_t reason, uint32_t info,
				     uint8_t exception, uint8_t flags)
{
	struct panic_data *pdata = get_panic_data_write();
	struct panic_data saved = *pdata;

	panic_set_reason(reason, info, exception);
	*expected = *pdata;
	expected->flags = flags;

	*pdata = saved;
}

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
	struct panic_data expected;

	zassert_is_null(pdata, NULL);
	panic_set_reason(PANIC_SW_WATCHDOG, 0x12, 0x34);

	panic_get_reason(&reason, &info, &exception);

	zassert_equal(PANIC_SW_WATCHDOG, reason);
	zassert_equal(0x12, info);
	zassert_equal(0x34, exception);

	pdata = panic_get_data();
	init_expected_panic_data(&expected, PANIC_SW_WATCHDOG, 0x12, 0x34,
				 DEFAULT_PANIC_FLAGS);
	zassert_mem_equal(pdata, &expected, sizeof(expected), NULL);

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
	struct panic_data *pdata;
	struct panic_data expected;

	/* Watchdog reset should result in any existing panic data being
	 * overwritten (if in RW)
	 */
	panic_set_reason(PANIC_SW_DIV_ZERO, 0x12, 0x34);

	/* Clear all reset flags and set them arbitrarily */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);

	pdata = panic_get_data();
	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		init_expected_panic_data(&expected, PANIC_SW_WATCHDOG_HARD, 0,
					 0, 0);
	} else {
		/* In RO, existing panic data should remain unchanged */
		init_expected_panic_data(&expected, PANIC_SW_DIV_ZERO, 0x12,
					 0x34, PANIC_DATA_FLAG_RO_IMAGE);
	}
	zassert_mem_equal(pdata, &expected, sizeof(expected), NULL);
}

ZTEST(panic, test_panic_data_init__watch_dog_warn_panic)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;
	struct panic_data *pdata;
	struct panic_data expected;

	/* Panic reason PANIC_SW_WATCHDOG_WARN should be switched
	 * to PANIC_SW_WATCHDOG after a watchdog reset (if in RW).
	 * Info and exception should be preserved.
	 */
	panic_set_reason(PANIC_SW_WATCHDOG_WARN, 0x12, 0x34);

	/* Set RO flag explicitly to test flag preservation */
	pdata = get_panic_data_write();
	pdata->flags &= ~PANIC_DATA_FLAG_RW_IMAGE;
	pdata->flags |= PANIC_DATA_FLAG_RO_IMAGE;

	/* Clear all reset flags and set them arbitrarily */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);

	pdata = panic_get_data();
	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		init_expected_panic_data(&expected, PANIC_SW_WATCHDOG, 0x12,
					 0x34, PANIC_DATA_FLAG_RO_IMAGE);
	} else {
		/* In RO, existing panic data remains */
		init_expected_panic_data(&expected, PANIC_SW_WATCHDOG_WARN,
					 0x12, 0x34, PANIC_DATA_FLAG_RO_IMAGE);
	}
	zassert_mem_equal(pdata, &expected, sizeof(expected), NULL);
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
	struct panic_data expected;

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

	pdata = panic_get_data();
	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		init_expected_panic_data(&expected, PANIC_SW_WATCHDOG_HARD, 0,
					 0, 0);
	} else {
		/* In RO, existing info remains even if already read */
		init_expected_panic_data(
			&expected, PANIC_SW_WATCHDOG, 0x12, 0x34,
			PANIC_DATA_FLAG_RO_IMAGE | PANIC_DATA_FLAG_OLD_HOSTCMD);
	}
	zassert_mem_equal(pdata, &expected, sizeof(expected), NULL);
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
	struct panic_data expected;

	panic_set_reason(PANIC_SW_WATCHDOG_HARD, 0x12, 0x34);
	pdata = get_panic_data_write();
	pdata->flags |= PANIC_DATA_FLAG_OLD_HOSTCMD;

	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);

	pdata = panic_get_data();
	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		init_expected_panic_data(&expected, PANIC_SW_WATCHDOG_HARD, 0,
					 0, 0);
	} else {
		init_expected_panic_data(
			&expected, PANIC_SW_WATCHDOG_HARD, 0x12, 0x34,
			PANIC_DATA_FLAG_RO_IMAGE | PANIC_DATA_FLAG_OLD_HOSTCMD);
	}
	zassert_mem_equal(pdata, &expected, sizeof(expected), NULL);
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
	struct panic_data *pdata;
	struct panic_data expected;

	panic_set_reason(0, 0x12, 0x34);
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();
	panic_get_reason(&reason, &info, &exception);

	pdata = panic_get_data();
	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		init_expected_panic_data(&expected, PANIC_SW_WATCHDOG_HARD, 0,
					 0, 0);
	} else {
		init_expected_panic_data(&expected, 0, 0x12, 0x34,
					 PANIC_DATA_FLAG_RO_IMAGE);
	}
	zassert_mem_equal(pdata, &expected, sizeof(expected), NULL);
}

ZTEST(panic, test_copy_esf_to_panic_data)
{
	struct arch_esf esf = { 0 };
	struct panic_data *pdata = panic_get_data();
	struct panic_data expected;
	uint8_t flags = DEFAULT_PANIC_FLAGS;

	if (PANIC_ARCH == PANIC_ARCH_CORTEX_M) {
		flags |= PANIC_DATA_FLAG_FRAME_VALID;
	}

	copy_esf_to_panic_data(&esf, pdata);

	pdata = panic_get_data();
	init_expected_panic_data(&expected, 0, 0, 0, flags);
	zassert_mem_equal(pdata, &expected, sizeof(expected), NULL);
}

ZTEST(panic, test_flush_panic_data_null)
{
	flush_panic_data(NULL);
}

ZTEST(panic, test_panic_get_reason_invalid)
{
	uint32_t reason = 0x1234;
	uint32_t info = 0x5678;
	uint8_t exception = 0x90;
	struct panic_data *pdata = get_panic_data_write();

	pdata->magic = 0;
	panic_get_reason(&reason, &info, &exception);
	zassert_equal(reason, 0);
	zassert_equal(info, 0);
	zassert_equal(exception, 0);

	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_version = 1;
	panic_get_reason(&reason, &info, &exception);
	zassert_equal(reason, 0);
	zassert_equal(info, 0);
	zassert_equal(exception, 0);
}
