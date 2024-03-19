/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "link_defs.h"
#include "mpu.h"
#include "ram_lock.h"
#include "string.h"
#include "system.h"
#include "task.h"
#include "test_util.h"

#include <stdbool.h>
#include <stdlib.h>

/* Select an address within the block region for the fetch lock test */
#define SPECIFIC_TEST_ADDRESS 0x100D5000

static int write_succeeds(uint32_t addr)
{
	*(volatile uint32_t *)addr = addr;

	if (*(volatile uint32_t *)addr != addr)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

test_static int verify_no_write(uint32_t addr)
{
	TEST_ASSERT(write_succeeds(addr) == EC_ERROR_UNKNOWN);
	return EC_SUCCESS;
}

test_static int verify_write(uint32_t addr)
{
	TEST_ASSERT(write_succeeds(addr) == EC_SUCCESS);
	return EC_SUCCESS;
}

#if defined(CHIP_VARIANT_NPCX9MFP)
/* Used to map to alias data ram */
#define ALIAS_DATA_RAM_SHIFT 0x10000000

/* Set a part of data RAM to fetch protection and use to check this region can
 * be written */
struct mpu_rw_regions data_ram_1 = { .num_regions = REGION_DATA_RAM,
				     .addr = { CONFIG_RAM_BASE },
				     .size = { 0x3000 } };

/* Set a part of data RAM to write protection and use to check this region
 * cannot be written */
struct mpu_rw_regions data_ram_2 = { .num_regions = REGION_STORAGE,
				     .addr = { (uint32_t)__shared_mem_buf },
				     .size = { 0x3000 } };

/* Use to check the protection region cannot be set because the address is not
 * aligned */
struct mpu_rw_regions invalid_code_reg_addr_not_aligned = {
	.num_regions = REGION_STORAGE,
	.addr = { 0x10059AB1 },
	.size = { 0x3000 }
};

/* Use to check the protection region cannot be set because the size is not
 * aligned */
struct mpu_rw_regions invalid_code_reg_size_not_aligned = {
	.num_regions = REGION_STORAGE,
	.addr = { 0x10058000 },
	.size = { 0x3A80 }
};

/* Set the fetch-protect region and use to check this region cannot be fetched
 */
struct mpu_rw_regions fetch_lock_ram = { .num_regions = REGION_DATA_RAM,
					 .addr = { 0x200D4000 },
					 .size = { 0x2000 } };
#else
#error "MPU info not defined for this chip. Please add it."
#endif

/*
 * Check the setting function will return error when the address is not
 * 4K aligned.
 */
test_static int test_ram_lock_config_lock_region_invalid_addr(void)
{
	TEST_EQ(ram_lock_config_lock_region(
			invalid_code_reg_addr_not_aligned.num_regions,
			invalid_code_reg_addr_not_aligned.addr[0],
			invalid_code_reg_addr_not_aligned.size[0]),
		-EC_ERROR_INVAL, "%d");

	return EC_SUCCESS;
}

/*
 * Check the setting function will return error when the size is not
 * 4K aligned.
 */
test_static int test_ram_lock_config_lock_region_invalid_size(void)
{
	TEST_EQ(ram_lock_config_lock_region(
			invalid_code_reg_size_not_aligned.num_regions,
			invalid_code_reg_size_not_aligned.addr[0],
			invalid_code_reg_size_not_aligned.size[0]),
		-EC_ERROR_INVAL, "%d");

	return EC_SUCCESS;
}

/* Set a part of the region as a protection area and return success */
test_static int test_ram_lock_config_lock_region(void)
{
	TEST_EQ(ram_lock_config_lock_region(REGION_DATA_RAM, CONFIG_RAM_BASE,
					    0x10000),
		EC_SUCCESS, "%d");
	TEST_EQ(ram_lock_config_lock_region(REGION_STORAGE,
					    CONFIG_PROGRAM_MEMORY_BASE +
						    CONFIG_RO_MEM_OFF,
					    0x10000),
		EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

/* Check the fetch-protect region can be written and the write-protect region
 * cannot be written */
test_static int test_ram_write_protect(void)
{
	TEST_EQ(verify_no_write(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RO_MEM_OFF),
		EC_SUCCESS, "%d");
	TEST_EQ(verify_write(CONFIG_RAM_BASE), EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

/* Set a part of the region as a protection area and return success */
test_static int test_ram_lock_config_lock_region_alias(void)
{
	TEST_EQ(ram_lock_config_lock_region(data_ram_1.num_regions,
					    data_ram_1.addr[0],
					    data_ram_1.size[0]),
		EC_SUCCESS, "%d");

	/* Set protection region with 4K aligned address and map to alias data
	 * ram */
	if (data_ram_2.addr[0] & 0xFFF) {
		data_ram_2.addr[0] = (data_ram_2.addr[0] & ~0xFFF) + 0x1000 -
				     ALIAS_DATA_RAM_SHIFT;
	}

	TEST_EQ(ram_lock_config_lock_region(data_ram_2.num_regions,
					    data_ram_2.addr[0],
					    data_ram_2.size[0]),
		EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

/* Check the fetch-protect region can be written and the write-protect region
 * cannot be written */
test_static int test_ram_alias_write_protect(void)
{
	TEST_EQ(verify_write(data_ram_1.addr[0]), EC_SUCCESS, "%d");
	TEST_EQ(verify_no_write(data_ram_2.addr[0]), EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

/* Use hibernate utility to test fetch lock; start and end are defined in linker
 * script */
extern unsigned int __flash_lpfw_start;
extern unsigned int __flash_lpfw_end;

/* Check the fetch-protect region cannot be fetched */
test_static int test_ram_fetch_protect(void)
{
	uintptr_t __ram_test_addr = SPECIFIC_TEST_ADDRESS;
	void (*__test_in_lpram)(void) = (void (*)(void))(__ram_test_addr);
	int i;

	/* Copy the __enter_hibernate_in_lpram instructions to
	 * SPECIFIC_TEST_ADDRESS */
	for (i = 0; i < &__flash_lpfw_end - &__flash_lpfw_start; i++) {
		*((uint32_t *)__test_in_lpram + i) = *(&__flash_lpfw_start + i);
	}

	/* Set the protection region for fetch operation */
	TEST_EQ(ram_lock_config_lock_region(fetch_lock_ram.num_regions,
					    fetch_lock_ram.addr[0],
					    fetch_lock_ram.size[0]),
		EC_SUCCESS, "%d");

	/* Execute instruction and it will cause busfault and reboot */
	__test_in_lpram();

	return EC_SUCCESS;
}

test_static void run_test_step1(void)
{
	RUN_TEST(test_ram_lock_config_lock_region_invalid_addr);
	RUN_TEST(test_ram_lock_config_lock_region_invalid_size);
	RUN_TEST(test_ram_lock_config_lock_region);
	RUN_TEST(test_ram_write_protect);
	RUN_TEST(test_ram_lock_config_lock_region_alias);
	RUN_TEST(test_ram_alias_write_protect);

	if (test_get_error_count()) {
		test_reboot_to_next_step(TEST_STATE_FAILED);
	} else {
		test_reboot_to_next_step(TEST_STATE_STEP_2);
	}
}

test_static void run_test_step2(void)
{
	test_set_next_step(TEST_STATE_PASSED);
	RUN_TEST(test_ram_fetch_protect);

	test_set_next_step(TEST_STATE_FAILED);
}

void test_run_step(uint32_t state)
{
	if (state & TEST_STATE_MASK(TEST_STATE_STEP_1)) {
		run_test_step1();
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_2)) {
		run_test_step2();
	}
}

int task_test(void *unused)
{
	test_run_multistep();
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	crec_msleep(30); /* Wait for TASK_ID_TEST to initialize */
	task_wake(TASK_ID_TEST);
}
