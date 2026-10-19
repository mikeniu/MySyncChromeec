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
#include "mpu_private.h"
#include "ram_lock.h"
#include "system.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#define TEST_STATE_STEP_2 (1 << 0)
#define TEST_STATE_FAIL (1 << 1)
#define MOCK_MPU_REGIONS 8
#define REGION_BITS 19
#define EXPECTED_MPU_RASR(attr, size_bit, srd) \
	(((uint32_t)(attr) << 16) | ((srd) << 8) | (((size_bit) - 1) << 1) | 1)
#define EXPECTED_MPU_CTRL \
	(MPU_CTRL_PRIVDEFEN | MPU_CTRL_HFNMIENA | MPU_CTRL_ENABLE)

void jump_to_image(uintptr_t init_addr);

char __iram_text_start;
extern char __iram_text_end __attribute__((alias("__iram_text_start")));
static struct {
	uint32_t ctrl;
	uint32_t base[MOCK_MPU_REGIONS];
	uint32_t rasr[MOCK_MPU_REGIONS];
} mock_mpu;
static int mock_is_locked, ram_lock_calls, mock_ram_lock_error;
static int sysjump_hook_mpu_active, resetvec_called, resetvec_irqs_disabled;

int mpu_update_region(uint8_t region, uint32_t addr, uint8_t size_bit,
		      uint16_t attr, uint8_t enable, uint8_t srd)
{
	if (!is_aligned(addr, BIT(size_bit)) || region >= MOCK_MPU_REGIONS ||
	    size_bit < MPU_SIZE_BITS_MIN)
		return -EC_ERROR_INVAL;
	mock_mpu.rasr[region] &= ~1;
	if (enable) {
		mock_mpu.base[region] = addr;
		mock_mpu.rasr[region] = EXPECTED_MPU_RASR(attr, size_bit, srd);
	}
	return EC_SUCCESS;
}

void mpu_enable(void)
{
	mock_mpu.ctrl |= EXPECTED_MPU_CTRL;
}

uint32_t mpu_get_type(void)
{
	return MOCK_MPU_REGIONS << 8;
}

static void test_sysjump_hook(void)
{
	sysjump_hook_mpu_active = (mock_mpu.rasr[REGION_DATA_RAM] & 1) &&
				  (mock_mpu.rasr[REGION_STORAGE] & 1);
}
DECLARE_HOOK(HOOK_SYSJUMP, test_sysjump_hook, HOOK_PRIO_LAST);

static void mock_resetvec(void)
{
	resetvec_called = 1;
	resetvec_irqs_disabled = !is_interrupt_enabled();
	interrupt_enable();
}

int system_is_locked(void)
{
	return mock_is_locked;
}

int ram_lock_config_lock_region(uint8_t region, uint32_t addr, uint32_t size)
{
	ram_lock_calls++;
	return mock_ram_lock_error;
}

void panic(const char *msg)
{
}

static int test_mpu_when_unlocked(void)
{
	mock_is_locked = 0;
	mock_ram_lock_error = EC_SUCCESS;
	ram_lock_calls = 0;
	memset(&mock_mpu, 0, sizeof(mock_mpu));
	system_disable_jump();
	TEST_EQ(mock_mpu.ctrl, EXPECTED_MPU_CTRL, "0x%08x");
	TEST_EQ(ram_lock_calls, 0, "%d");
	TEST_EQ(mock_mpu.base[REGION_DATA_RAM],
		align_down_to_bits(CONFIG_RAM_BASE, REGION_BITS), "0x%08x");
	TEST_EQ(mock_mpu.rasr[REGION_DATA_RAM],
		EXPECTED_MPU_RASR(MPU_ATTR_XN | MPU_ATTR_RW_RW |
					  MPU_ATTR_INTERNAL_SRAM,
				  REGION_BITS, 0xe7),
		"0x%08x");
	TEST_EQ(mock_mpu.base[REGION_DATA_RAM2], 0x0, "0x%08x");
	TEST_EQ(mock_mpu.rasr[REGION_DATA_RAM2], 0x0, "0x%08x");
	TEST_EQ(mock_mpu.base[REGION_DATA_RAM_TEXT], 0x0, "0x%08x");
	TEST_EQ(mock_mpu.rasr[REGION_DATA_RAM_TEXT], 0x0, "0x%08x");
	TEST_EQ(mock_mpu.base[REGION_STORAGE],
		align_down_to_bits(CONFIG_PROGRAM_MEMORY_BASE, REGION_BITS),
		"0x%08x");
	TEST_EQ(mock_mpu.rasr[REGION_STORAGE],
		EXPECTED_MPU_RASR(MPU_ATTR_RO_NO | MPU_ATTR_INTERNAL_SRAM,
				  REGION_BITS, 0x7f),
		"0x%08x");
	TEST_EQ(mock_mpu.base[REGION_STORAGE2],
		CONFIG_PROGRAM_MEMORY_BASE + BIT(REGION_BITS - 3), "0x%08x");
	TEST_EQ(mock_mpu.rasr[REGION_STORAGE2],
		EXPECTED_MPU_RASR(MPU_ATTR_RO_NO | MPU_ATTR_INTERNAL_SRAM,
				  REGION_BITS, 0xf8),
		"0x%08x");
	TEST_EQ(mpu_config_region(REGION_CHIP_RESERVED, CORTEX_M_SRAM_BASE,
				  1024, MPU_ATTR_RW_RW | MPU_ATTR_INTERNAL_SRAM,
				  1),
		EC_SUCCESS, "%d");
	sysjump_hook_mpu_active = 0;
	resetvec_called = 0;
	resetvec_irqs_disabled = 0;
	jump_to_image((uintptr_t)mock_resetvec);
	TEST_EQ(sysjump_hook_mpu_active, 1, "%d");
	TEST_EQ(resetvec_called, 1, "%d");
	TEST_EQ(resetvec_irqs_disabled, 1, "%d");
	TEST_EQ(mock_mpu.ctrl, EXPECTED_MPU_CTRL, "0x%08x");
	TEST_EQ(mock_mpu.rasr[REGION_CHIP_RESERVED] & 1, 1, "%d");
	TEST_EQ(mock_mpu.rasr[REGION_DATA_RAM] & 1, 0, "%d");
	TEST_EQ(mock_mpu.rasr[REGION_DATA_RAM2] & 1, 0, "%d");
	TEST_EQ(mock_mpu.rasr[REGION_STORAGE] & 1, 0, "%d");
	TEST_EQ(mock_mpu.rasr[REGION_STORAGE2] & 1, 0, "%d");
	TEST_EQ(mock_mpu.rasr[REGION_DATA_RAM_TEXT] & 1, 0, "%d");
	mock_is_locked = 1;
	mock_ram_lock_error = EC_ERROR_INVAL;
	ram_lock_calls = 0;
	memset(&mock_mpu, 0, sizeof(mock_mpu));
	system_disable_jump();
	TEST_EQ(ram_lock_calls, 1, "%d");
	TEST_EQ(mock_mpu.ctrl, 0, "0x%08x");
	mock_is_locked = 1;
	mock_ram_lock_error = EC_SUCCESS;
	ram_lock_calls = 0;
	memset(&mock_mpu, 0, sizeof(mock_mpu));
	system_disable_jump();
	TEST_EQ(mock_mpu.ctrl, EXPECTED_MPU_CTRL, "0x%08x");
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
