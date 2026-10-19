/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test system_common.
 */

#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hooks.h"
#include "host_command.h"
#include "mpu.h"
#include "ram_lock.h"
#include "system.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#define TEST_STATE_STEP_2 (1 << 0)
#define TEST_STATE_FAIL (1 << 1)

static uint32_t mock_mpu_type = 0x00000800; /* 8 unified regions */
static uint32_t mock_mpu_ctrl;
static uint32_t mock_mpu_number;
static uint32_t mock_mpu_base[16];
static uint32_t mock_mpu_size_attr[16];
char __iram_text_start;
extern char __iram_text_end __attribute__((alias("__iram_text_start")));
static int mock_is_locked;
static int ram_lock_calls;

int system_is_locked(void)
{
	return mock_is_locked;
}

int ram_lock_config_lock_region(uint8_t region, uint32_t addr, uint32_t size)
{
	ram_lock_calls++;
	return EC_SUCCESS;
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

#undef MPU_TYPE
#undef MPU_CTRL
#undef MPU_NUMBER
#undef MPU_BASE
#undef MPU_SIZE
#undef MPU_ATTR
#define MPU_TYPE mock_mpu_type
#define MPU_CTRL mock_mpu_ctrl
#define MPU_NUMBER mock_mpu_number
#define MPU_BASE (mock_mpu_base[mock_mpu_number & 0xf])
#define MPU_SIZE (*((uint16_t *)&mock_mpu_size_attr[mock_mpu_number & 0xf]))
#define MPU_ATTR (*((uint16_t *)&mock_mpu_size_attr[mock_mpu_number & 0xf] + 1))

#undef CONFIG_PROGRAM_MEMORY_BASE
#define CONFIG_PROGRAM_MEMORY_BASE 0x10070000
#undef CONFIG_RO_MEM_OFF
#define CONFIG_RO_MEM_OFF 0
#include "../core/cortex-m/mpu.c"

static int test_mpu_when_unlocked(void)
{
	/* Unlocked system should still configure and enable Cortex-M MPU */
	mock_is_locked = 0;
	mock_mpu_ctrl = 0;
	mock_mpu_number = 0;
	memset(mock_mpu_base, 0, sizeof(mock_mpu_base));
	memset(mock_mpu_size_attr, 0, sizeof(mock_mpu_size_attr));
	ram_lock_calls = 0;

	system_disable_jump();

	TEST_BITS_SET(mock_mpu_ctrl, MPU_CTRL_ENABLE);
	/* Sticky RAM_LOCK must not be engaged when system is unlocked */
	TEST_EQ(ram_lock_calls, 0, "%d");

	/* Unused SRAM in code RAM aperture must be marked XN (REGION_CODE_RAM)
	 */
	TEST_EQ(mock_mpu_base[REGION_CODE_RAM], 0x10000000, "0x%08x");
	TEST_BITS_SET(mock_mpu_size_attr[REGION_CODE_RAM] >> 16, MPU_ATTR_XN);
	TEST_BITS_SET(mock_mpu_size_attr[REGION_CODE_RAM], 1);

	/* Active code RAM must be marked read-only executable (REGION_STORAGE)
	 */
	TEST_EQ(mock_mpu_base[REGION_STORAGE], 0x10000000, "0x%08x");
	TEST_EQ(mock_mpu_base[REGION_STORAGE2], 0x10080000, "0x%08x");
	TEST_BITS_CLEARED(mock_mpu_size_attr[REGION_STORAGE] >> 16,
			  MPU_ATTR_XN);
	TEST_BITS_SET(mock_mpu_size_attr[REGION_STORAGE], 1);
	TEST_BITS_SET(mock_mpu_size_attr[REGION_STORAGE2], 1);

	/* SYSJUMP hook must disable MPU so unlocked sysjump/lfw can run */
	hook_notify(HOOK_SYSJUMP);
	TEST_BITS_CLEARED(mock_mpu_ctrl, MPU_CTRL_ENABLE);

	/* Locked system must engage both RAM_LOCK and MPU */
	mock_is_locked = 1;
	system_disable_jump();
	TEST_BITS_SET(mock_mpu_ctrl, MPU_CTRL_ENABLE);
	TEST_EQ(ram_lock_calls, 2, "%d");

	return EC_SUCCESS;
}

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
	if (test_mpu_when_unlocked() != EC_SUCCESS)
		test_fail();
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
