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
#include "hooks.h"
#include "host_command.h"
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

struct panic_test_fixture {
	struct panic_data saved_pdata;
};

static struct panic_data hard_watchdog_panic;

static void test_panic_set_reason(uint32_t reason, uint32_t info,
				  uint8_t exception)
{
	struct panic_data *const pdata = panic_data_reset(NULL);

	panic_set_reason_reg(pdata, reason);
	panic_set_info_reg(pdata, info);
	panic_set_exception_reg(pdata, exception);

	panic_data_finalize(pdata);
}

static void panic_setup_hard_watchdog_panic(void)
{
	panic_data_reset(&hard_watchdog_panic);
	hard_watchdog_panic.flags &=
		~(PANIC_DATA_FLAG_RW_IMAGE | PANIC_DATA_FLAG_RO_IMAGE);
	panic_set_reason_reg(&hard_watchdog_panic, PANIC_SW_WATCHDOG_HARD);
	panic_data_finalize(&hard_watchdog_panic);
}

static void *panic_test_setup(void)
{
	static struct panic_test_fixture panic_fixture = { 0 };

	panic_setup_hard_watchdog_panic();
	return &panic_fixture;
}

static void panic_before(void *state)
{
	struct panic_test_fixture *fixture = state;
	struct panic_data *pdata = get_panic_data_write();

	ARG_UNUSED(state);
	system_clear_reset_flags(-1);

	fixture->saved_pdata = *pdata;
	memset(pdata, 0, CONFIG_PANIC_DATA_SIZE);
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
	struct panic_data *pdata = panic_get_data();

	zassert_is_null(pdata, NULL);
	test_panic_set_reason(PANIC_SW_WATCHDOG, 0x12, 0x34);

	pdata = panic_get_data();
	zassert_not_null(pdata, NULL);
	zassert_equal(PANIC_SW_WATCHDOG, panic_get_reason_reg(pdata));
	zassert_equal(0x12, panic_get_info_reg(pdata));
	zassert_equal(0x34, panic_get_exception_reg(pdata));

	zassert_equal(pdata->struct_version, PANIC_DATA_VERSION);
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
	/* Watchdog reset should result in any existing panic data being
	 * overwritten (if in RW)
	 */
	test_panic_set_reason(PANIC_SW_DIV_ZERO, 0x12, 0x34);
	const struct panic_data original_pdata = *panic_get_data();

	/* Simulate a watchdog reset cause */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();

	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		zassert_mem_equal(panic_get_data(), &hard_watchdog_panic,
				  sizeof(hard_watchdog_panic), NULL);
	} else {
		/* In RO, existing panic data should remain unchanged */
		zassert_mem_equal(panic_get_data(), &original_pdata,
				  sizeof(original_pdata), NULL);
	}
}

ZTEST(panic, test_panic_data_init__watch_dog_warn_panic)
{
	/* Panic reason PANIC_SW_WATCHDOG_WARN should be switched
	 * to PANIC_SW_WATCHDOG after a watchdog reset (if in RW).
	 * Info and exception should be preserved.
	 */
	test_panic_set_reason(PANIC_SW_WATCHDOG_WARN, 0x12, 0x34);

	/* Set RO flag explicitly to test flag preservation */
	struct panic_data *pdata = get_panic_data_write();
	pdata->flags &= ~PANIC_DATA_FLAG_RW_IMAGE;
	pdata->flags |= PANIC_DATA_FLAG_RO_IMAGE;
	const struct panic_data original_pdata = *pdata;

	/* Simulate a watchdog reset cause */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();

	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		struct panic_data expected_pdata = original_pdata;

		panic_set_reason_reg(&expected_pdata, PANIC_SW_WATCHDOG);
		zassert_mem_equal(panic_get_data(), &expected_pdata,
				  sizeof(expected_pdata), NULL);
	} else {
		/* In RO, existing panic data should remain unchanged */
		zassert_mem_equal(panic_get_data(), &original_pdata,
				  sizeof(original_pdata), NULL);
	}
}

ZTEST(panic, test_panic_data_init__watch_dog_warn_panic_already_read)
{
	test_panic_set_reason(PANIC_SW_WATCHDOG_WARN, 0x12, 0x34);
	struct panic_data *pdata = get_panic_data_write();
	pdata->flags |= PANIC_DATA_FLAG_OLD_HOSTCMD;
	const struct panic_data original_pdata = *pdata;

	/* Simulate a watchdog reset cause */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();

	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		zassert_mem_equal(panic_get_data(), &hard_watchdog_panic,
				  sizeof(hard_watchdog_panic), NULL);
	} else {
		/* In RO, existing panic data should remain unchanged */
		zassert_mem_equal(panic_get_data(), &original_pdata,
				  sizeof(original_pdata), NULL);
	}
}

ZTEST(panic, test_panic_data_init__watch_dog_panic_already_initialized)
{
	/* Watchdog reset should not overwrite panic info if already filled
	 * in with watchdog panic info that HAS NOT been read by host
	 */
	test_panic_set_reason(PANIC_SW_WATCHDOG, 0x12, 0x34);
	const struct panic_data original_pdata = *panic_get_data();

	/* Simulate a watchdog reset cause */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();

	zassert_mem_equal(panic_get_data(), &original_pdata,
			  sizeof(original_pdata), NULL);
}

ZTEST(panic, test_panic_data_init__watch_dog_hard_panic_already_initialized)
{
	/* Watchdog reset should not overwrite panic info if already filled
	 * in with watchdog hard panic info that HAS NOT been read by host
	 */
	test_panic_set_reason(PANIC_SW_WATCHDOG_HARD, 0x12, 0x34);
	const struct panic_data original_pdata = *panic_get_data();

	/* Simulate a watchdog reset cause */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();

	zassert_mem_equal(panic_get_data(), &original_pdata,
			  sizeof(original_pdata), NULL);
}

ZTEST(panic, test_panic_data_init__watch_dog_panic_already_read)
{
	/* Watchdog reset should overwrite panic info if already filled
	 * in with watchdog panic info that HAS been read by host (if in RW)
	 */
	test_panic_set_reason(PANIC_SW_WATCHDOG, 0x12, 0x34);
	struct panic_data *pdata = get_panic_data_write();
	pdata->flags |= PANIC_DATA_FLAG_OLD_HOSTCMD;
	const struct panic_data original_pdata = *pdata;

	zassert_false(panic_data_is_new());

	/* Simulate a watchdog reset cause */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();

	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		zassert_true(panic_data_is_new());
		zassert_mem_equal(panic_get_data(), &hard_watchdog_panic,
				  sizeof(hard_watchdog_panic), NULL);
	} else {
		/* In RO, existing panic data should remain unchanged */
		zassert_false(panic_data_is_new());
		zassert_mem_equal(panic_get_data(), &original_pdata,
				  sizeof(original_pdata), NULL);
	}
}

ZTEST(panic, test_panic_data_init__no_watchdog_reset_flag)
{
	test_panic_set_reason(PANIC_SW_DIV_ZERO, 0x12, 0x34);
	const struct panic_data original_pdata = *panic_get_data();

	/* Simulate a power-on reset cause */
	system_set_reset_flags(EC_RESET_FLAG_POWER_ON);
	panic_data_init();

	zassert_mem_equal(panic_get_data(), &original_pdata,
			  sizeof(original_pdata), NULL);
}

ZTEST(panic, test_panic_data_init__watch_dog_hard_panic_already_read)
{
	test_panic_set_reason(PANIC_SW_WATCHDOG_HARD, 0x12, 0x34);
	struct panic_data *pdata = get_panic_data_write();
	pdata->flags |= PANIC_DATA_FLAG_OLD_HOSTCMD;
	const struct panic_data original_pdata = *pdata;

	/* Simulate a watchdog reset cause */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();

	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		zassert_mem_equal(panic_get_data(), &hard_watchdog_panic,
				  sizeof(hard_watchdog_panic), NULL);
	} else {
		/* In RO, existing panic data should remain unchanged */
		zassert_mem_equal(panic_get_data(), &original_pdata,
				  sizeof(original_pdata), NULL);
	}
}

ZTEST(panic, test_panic_data_init__invalid_sw_reason)
{
	test_panic_set_reason(0, 0x12, 0x34);
	const struct panic_data original_pdata = *panic_get_data();

	/* Simulate a power-on reset cause */
	system_set_reset_flags(EC_RESET_FLAG_POWER_ON);
	panic_data_init();

	zassert_mem_equal(panic_get_data(), &original_pdata,
			  sizeof(original_pdata), NULL);
}

ZTEST(panic, test_panic_data_init__old_panic_no_watchdog)
{
	test_panic_set_reason(PANIC_SW_DIV_ZERO, 0x12, 0x34);
	struct panic_data *pdata = get_panic_data_write();
	pdata->flags |= PANIC_DATA_FLAG_OLD_HOSTCMD;
	const struct panic_data original_pdata = *pdata;

	zassert_false(panic_data_is_new());

	/* Simulate a power-on reset cause */
	system_set_reset_flags(EC_RESET_FLAG_POWER_ON);
	panic_data_init();

	zassert_false(panic_data_is_new());
	zassert_mem_equal(panic_get_data(), &original_pdata,
			  sizeof(original_pdata), NULL);
}

ZTEST(panic, test_panic_data_init__invalid_sw_reason_watchdog)
{
	test_panic_set_reason(0, 0x12, 0x34);
	const struct panic_data original_pdata = *panic_get_data();

	/* Simulate a watchdog reset cause */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	panic_data_init();

	if (IS_ENABLED(CONFIG_CROS_EC_RW)) {
		zassert_mem_equal(panic_get_data(), &hard_watchdog_panic,
				  sizeof(hard_watchdog_panic), NULL);
	} else {
		/* In RO, existing panic data should remain unchanged */
		zassert_mem_equal(panic_get_data(), &original_pdata,
				  sizeof(original_pdata), NULL);
	}
}

ZTEST(panic, test_copy_esf_to_panic_data)
{
	struct arch_esf esf = { 0 };
	struct panic_data *pdata = get_panic_data_write();
	uint8_t expected_flags = IS_ENABLED(CONFIG_CROS_EC_RW) ?
					 PANIC_DATA_FLAG_RW_IMAGE :
					 PANIC_DATA_FLAG_RO_IMAGE;

	if (IS_ENABLED(CONFIG_ARM)) {
		expected_flags |= PANIC_DATA_FLAG_FRAME_VALID;
	}

	copy_esf_to_panic_data(&esf, pdata);

	pdata = panic_get_data();
	zassert_not_null(pdata, NULL);
	zassert_equal(pdata->magic, PANIC_DATA_MAGIC);
	zassert_equal(pdata->struct_version, PANIC_DATA_VERSION);
	zassert_equal(pdata->struct_size, CONFIG_PANIC_DATA_SIZE);
	zassert_equal(pdata->flags, expected_flags);
}

ZTEST(panic, test_panic_data_reset_and_finalize)
{
	struct panic_data *pdata = get_panic_data_write();

	/* Pre-populate with old magic to ensure reset clears it */
	pdata->magic = PANIC_DATA_MAGIC;

	struct panic_data *res = panic_data_reset(pdata);
	zassert_equal(res, pdata);
	zassert_equal(pdata->magic, 0, "magic should be 0 after reset, got %x",
		      pdata->magic);
	zassert_equal(pdata->struct_version, PANIC_DATA_VERSION);
	zassert_equal(pdata->struct_size, CONFIG_PANIC_DATA_SIZE);

	panic_data_finalize(pdata);
	zassert_equal(pdata->magic, PANIC_DATA_MAGIC,
		      "magic should be PANIC_DATA_MAGIC after finalize, got %x",
		      pdata->magic);
}

ZTEST(panic, test_copy_esf_to_panic_data_null)
{
	struct arch_esf esf = { 0 };
	uint8_t expected_flags = IS_ENABLED(CONFIG_CROS_EC_RW) ?
					 PANIC_DATA_FLAG_RW_IMAGE :
					 PANIC_DATA_FLAG_RO_IMAGE;

	if (IS_ENABLED(CONFIG_ARM)) {
		expected_flags |= PANIC_DATA_FLAG_FRAME_VALID;
	}

	copy_esf_to_panic_data(&esf, NULL);

	struct panic_data *pdata = panic_get_data();
	zassert_not_null(pdata, NULL);
	zassert_equal(pdata->magic, PANIC_DATA_MAGIC);
	zassert_equal(pdata->struct_version, PANIC_DATA_VERSION);
	zassert_equal(pdata->struct_size, CONFIG_PANIC_DATA_SIZE);
	zassert_equal(pdata->flags, expected_flags);
}

ZTEST(panic, test_panic_data_reset_null)
{
	struct panic_data *expected_pdata = get_panic_data_write();
	expected_pdata->magic = PANIC_DATA_MAGIC;

	struct panic_data *res = panic_data_reset(NULL);
	zassert_equal(res, expected_pdata);
	zassert_equal(res->magic, 0, "magic should be 0 after reset, got %x",
		      res->magic);
	zassert_equal(res->struct_version, PANIC_DATA_VERSION);
	zassert_equal(res->struct_size, CONFIG_PANIC_DATA_SIZE);
}

ZTEST(panic, test_panic_data_finalize_null)
{
	panic_data_finalize(NULL);
}

ZTEST(panic, test_panic_reason_reg)
{
	struct panic_data test_pdata = { 0 };

	zassert_equal(panic_get_reason_reg(NULL), 0);

	panic_set_reason_reg(NULL, PANIC_SW_DIV_ZERO);

	panic_set_reason_reg(&test_pdata, PANIC_SW_DIV_ZERO);
	zassert_equal(panic_get_reason_reg(&test_pdata), PANIC_SW_DIV_ZERO);

	panic_set_reason_reg(&test_pdata, PANIC_SW_WATCHDOG);
	zassert_equal(panic_get_reason_reg(&test_pdata), PANIC_SW_WATCHDOG);
}

ZTEST(panic, test_panic_field_accessors)
{
	struct panic_data test_pdata = { 0 };

	zassert_equal(panic_get_info_reg(NULL), 0);
	panic_set_info_reg(NULL, 0x1234);
	panic_set_info_reg(&test_pdata, 0x12345678);
	zassert_equal(panic_get_info_reg(&test_pdata), 0x12345678);

	zassert_equal(panic_get_exception_reg(NULL), 0);
	panic_set_exception_reg(NULL, 0x5a);
	panic_set_exception_reg(&test_pdata, 0x5a);
	zassert_equal(panic_get_exception_reg(&test_pdata), 0x5a);
}

ZTEST(panic, test_panic_host_event)
{
	struct panic_data *pdata;

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_PANIC_HOST_EVENT)) {
		ztest_test_skip();
	}

	host_clear_events(CONFIG_HOST_EVENT_REPORT_MASK);
	zassert_false(host_is_event_set(EC_HOST_EVENT_PANIC), NULL);

	/* Set panic reason */
	test_panic_set_reason(PANIC_SW_DIV_ZERO, 0, 0);

	/* Trigger HOOK_CHIPSET_STARTUP */
	hook_notify(HOOK_CHIPSET_STARTUP);

	/* Verify host event is set and flag is set in panic data */
	zassert_true(host_is_event_set(EC_HOST_EVENT_PANIC), NULL);
	pdata = panic_get_data();
	zassert_not_null(pdata, NULL);
	zassert_true(pdata->flags & PANIC_DATA_FLAG_OLD_HOSTEVENT, NULL);

	/* Verify subsequent HOOK_CHIPSET_STARTUP does not re-set host event */
	host_clear_events(CONFIG_HOST_EVENT_REPORT_MASK);
	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_false(host_is_event_set(EC_HOST_EVENT_PANIC), NULL);
}
