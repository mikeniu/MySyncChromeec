/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test system_common.
 */

#include "common.h"
#include "console.h"
#include "host_command.h"
#ifdef BOARD_HOST
#include "cpu.h"
#include "hooks.h"
#include "mock/mpu_mock.h"
#include "mpu.h"
#include "mpu_private.h"
#include "ram_lock.h"
#endif
#include "system.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#define TEST_STATE_STEP_2 (1 << 0)
#define TEST_STATE_FAIL (1 << 1)

#ifdef BOARD_HOST
#define EXPECTED_MPU_RASR(attr, size_bit, srd) \
	(((uint32_t)(attr) << 16) | ((srd) << 8) | (((size_bit) - 1) << 1) | 1)
#define STORAGE1_SIZE_BITS 19
#define STORAGE1_SRD 0x7f
#define STORAGE2_SIZE_BITS 19
#define STORAGE2_SRD 0xf8
#define DATA_RAM_SIZE_BITS 19
#define DATA_RAM_SRD 0xe7
#define EXPECTED_MPU_CTRL \
	(MPU_CTRL_PRIVDEFEN | MPU_CTRL_HFNMIENA | MPU_CTRL_ENABLE)
#define EXPECTED_LOCKED_REGION_COUNT 2

char __iram_text_start;
extern char __iram_text_end __attribute__((alias("__iram_text_start")));
static int mock_is_locked;
static int ram_lock_calls;
static int mock_ram_lock_error;

int system_is_locked(void)
{
	return mock_is_locked;
}

int ram_lock_config_lock_region(uint8_t region, uint32_t addr, uint32_t size)
{
	ram_lock_calls++;
	return mock_ram_lock_error;
}

enum ec_image system_get_shrspi_image_copy(void)
{
	return EC_IMAGE_RO;
}

uint32_t system_get_lfw_address(void)
{
	return 0;
}

void system_set_image_copy(enum ec_image copy)
{
}

void panic(const char *msg)
{
}

void crec_flash_lock_mapped_storage(int lock)
{
}

void cpu_enable_caches(void)
{
}

static int test_mpu_when_unlocked(void)
{
	/* Unlocked system should still configure and enable Cortex-M MPU */
	mock_is_locked = 0;
	mock_ram_lock_error = EC_SUCCESS;
	ram_lock_calls = 0;
	mock_mpu_reset();

	system_disable_jump();

	TEST_EQ(mock_mpu.ctrl, EXPECTED_MPU_CTRL, "0x%08x");
	/* Sticky RAM_LOCK must not be engaged when system is unlocked */
	TEST_EQ(ram_lock_calls, 0, "%d");

	/* Check data RAM configuration */
	TEST_EQ(mock_mpu.base[REGION_DATA_RAM],
		align_down_to_bits(CONFIG_RAM_BASE, DATA_RAM_SIZE_BITS),
		"0x%08x");
	TEST_EQ(mock_mpu.rasr[REGION_DATA_RAM],
		EXPECTED_MPU_RASR(MPU_ATTR_XN | MPU_ATTR_RW_RW |
					  MPU_ATTR_INTERNAL_SRAM,
				  DATA_RAM_SIZE_BITS, DATA_RAM_SRD),
		"0x%08x");
	TEST_EQ(mock_mpu.base[REGION_DATA_RAM2], 0x0, "0x%08x");
	TEST_EQ(mock_mpu.rasr[REGION_DATA_RAM2], 0x0, "0x%08x");

	/* Check data RAM text exemption (size is zero) */
	TEST_EQ(mock_mpu.base[REGION_DATA_RAM_TEXT], 0x0, "0x%08x");
	TEST_EQ(mock_mpu.rasr[REGION_DATA_RAM_TEXT], 0x0, "0x%08x");

	/*
	 * Active code RAM must be marked read-only executable (REGION_STORAGE
	 * and REGION_STORAGE2).
	 */
	TEST_EQ(mock_mpu.base[REGION_STORAGE],
		align_down_to_bits(CONFIG_PROGRAM_MEMORY_BASE,
				   STORAGE1_SIZE_BITS),
		"0x%08x");
	TEST_EQ(mock_mpu.rasr[REGION_STORAGE],
		EXPECTED_MPU_RASR(MPU_ATTR_RO_NO | MPU_ATTR_INTERNAL_SRAM,
				  STORAGE1_SIZE_BITS, STORAGE1_SRD),
		"0x%08x");
	TEST_EQ(mock_mpu.base[REGION_STORAGE2],
		CONFIG_PROGRAM_MEMORY_BASE + BIT(STORAGE1_SIZE_BITS - 3),
		"0x%08x");
	TEST_EQ(mock_mpu.rasr[REGION_STORAGE2],
		EXPECTED_MPU_RASR(MPU_ATTR_RO_NO | MPU_ATTR_INTERNAL_SRAM,
				  STORAGE2_SIZE_BITS, STORAGE2_SRD),
		"0x%08x");

	/* SYSJUMP hook must disable MPU so unlocked sysjump/lfw can run */
	hook_notify(HOOK_SYSJUMP);
	TEST_EQ(mock_mpu.ctrl, 0, "0x%08x");

	/* If RAM_LOCK fails on a locked system, MPU must not be enabled */
	mock_is_locked = 1;
	mock_ram_lock_error = EC_ERROR_INVAL;
	ram_lock_calls = 0;
	mock_mpu_reset();
	system_disable_jump();
	TEST_EQ(ram_lock_calls, 1, "%d");
	TEST_EQ(mock_mpu.ctrl, 0, "0x%08x");

	/* Locked system must engage both RAM_LOCK and MPU */
	mock_is_locked = 1;
	mock_ram_lock_error = EC_SUCCESS;
	ram_lock_calls = 0;
	mock_mpu_reset();
	system_disable_jump();
	TEST_EQ(mock_mpu.ctrl, EXPECTED_MPU_CTRL, "0x%08x");
	TEST_EQ(ram_lock_calls, EXPECTED_LOCKED_REGION_COUNT, "%d");

	return EC_SUCCESS;
}
#endif /* BOARD_HOST */

static int test_reboot_on_shutdown(void)
{
	struct ec_params_reboot_ec params;

	/* Fails if the system reboots unexpectedly */
	system_set_scratchpad(TEST_STATE_FAIL);

	test_chipset_on();
	crec_msleep(30);

	params.cmd = EC_REBOOT_COLD;
	params.flags = EC_REBOOT_FLAG_ON_AP_SHUTDOWN;

	TEST_EQ(test_send_host_command(EC_CMD_REBOOT_EC, 0, &params,
				       sizeof(params), NULL, 0),
		EC_SUCCESS, "%d");

	system_set_scratchpad(TEST_STATE_STEP_2);
	test_chipset_off();
	crec_msleep(30);

	/* Shouldn't reach here */
	return EC_ERROR_UNKNOWN;
}

static int test_cancel_reboot(void)
{
	struct ec_params_reboot_ec params;

	/* Fails if the system reboots unexpectedly */
	system_set_scratchpad(TEST_STATE_FAIL);

	test_chipset_on();
	crec_msleep(30);

	params.cmd = EC_REBOOT_COLD;
	params.flags = EC_REBOOT_FLAG_ON_AP_SHUTDOWN;

	TEST_EQ(test_send_host_command(EC_CMD_REBOOT_EC, 0, &params,
				       sizeof(params), NULL, 0),
		EC_SUCCESS, "%d");

	params.cmd = EC_REBOOT_CANCEL;
	params.flags = 0;

	TEST_EQ(test_send_host_command(EC_CMD_REBOOT_EC, 0, &params,
				       sizeof(params), NULL, 0),
		EC_SUCCESS, "%d");

	test_chipset_off();
	crec_msleep(30);

	return EC_SUCCESS;
}

static void run_test_step1(void)
{
#ifdef BOARD_HOST
	if (test_mpu_when_unlocked() != EC_SUCCESS)
		test_fail();
#endif
	if (test_reboot_on_shutdown() != EC_SUCCESS)
		test_fail();
}

static void run_test_step2(void)
{
	if (test_cancel_reboot() != EC_SUCCESS)
		test_fail();

	system_set_scratchpad(0);
	test_pass();
}

static void fail_and_clean_up(void)
{
	system_set_scratchpad(0);
	test_fail();
}

void run_test(int argc, const char **argv)
{
	uint32_t state = 0;

	/* The return value isn't checked here on purpose. The scratchpad file
	 * may exist from a previous run or it may be in a clean state. A
	 * previous run of this test should reset the scratchpad value to 0
	 * regardless of the final result.
	 */
	system_get_scratchpad(&state);

	test_reset();

	if (state == 0)
		run_test_step1();
	else if (state & TEST_STATE_STEP_2)
		run_test_step2();
	else if (state & TEST_STATE_FAIL)
		fail_and_clean_up();
}
