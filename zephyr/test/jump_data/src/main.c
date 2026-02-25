/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "panic.h"
#include "sysjump.h"
#include "system.h"

#include <string.h>

#include <zephyr/ztest.h>

extern char mock_jump_data[CONFIG_PLATFORM_EC_PRESERVED_END_OF_RAM_SIZE];

struct jump_data *get_jump_data(void);

/*
 * Redefine jump_tag since it's static in common/system.c
 */
struct jump_tag {
	uint16_t tag; /* Tag ID */
	uint8_t data_size; /* Size of data which follows */
	uint8_t data_version; /* Data version */
};

#define ROUNDUP4(x) (((x) + 3) & ~3)

static void jump_data_before(void *data)
{
	memset(mock_jump_data, 0, sizeof(mock_jump_data));
	system_common_reset_state();
}

static struct jump_data *get_test_jdata_ptr(void)
{
	return (struct jump_data *)(mock_jump_data + sizeof(mock_jump_data) -
				    sizeof(struct jump_data));
}

ZTEST(jump_data, test_init_no_jump_data)
{
	struct jump_data *jdata = get_test_jdata_ptr();

	/* Set up invalid magic */
	jdata->magic = 0xdeadbeef;
	jdata->version = JUMP_DATA_VERSION;

	system_common_pre_init();

	/* Should be cleared */
	zassert_equal(jdata->magic, 0);
	zassert_equal(jdata->version, 0);
	zassert_equal(system_jumped_to_this_image(), 0);
}

ZTEST(jump_data, test_init_v3_jump_data)
{
	struct jump_data *jdata = get_test_jdata_ptr();

	jdata->magic = JUMP_DATA_MAGIC;
	jdata->version = 3;
	jdata->struct_size = sizeof(struct jump_data);
	jdata->jump_tag_total = 0;
	jdata->reset_flags = EC_RESET_FLAG_POWER_ON;

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	zassert_equal(system_get_reset_flags(),
		      EC_RESET_FLAG_POWER_ON | EC_RESET_FLAG_SYSJUMP);

	/* Magic should be cleared by pre_init */
	zassert_equal(jdata->magic, 0);
	zassert_equal(jdata->struct_size, sizeof(struct jump_data));
}

ZTEST(jump_data, test_init_v1_jump_data)
{
	/* V1 struct size is 12: reset_flags, version, magic */
	uint32_t *v1_ptr =
		(uint32_t *)(mock_jump_data + sizeof(mock_jump_data) - 12);
	v1_ptr[0] = EC_RESET_FLAG_POWER_ON;
	v1_ptr[1] = 1;
	v1_ptr[2] = JUMP_DATA_MAGIC;

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	zassert_equal(system_get_reset_flags(),
		      EC_RESET_FLAG_POWER_ON | EC_RESET_FLAG_SYSJUMP);

	struct jump_data *jdata = get_test_jdata_ptr();
	zassert_equal(jdata->version, 1);
	zassert_equal(jdata->struct_size, sizeof(struct jump_data));
	zassert_equal(jdata->jump_tag_total, 0);
}

ZTEST(jump_data, test_init_v2_jump_data)
{
	/* V2 struct size is 16: jump_tag_total, reset_flags, version, magic */
	uint32_t *v2_ptr =
		(uint32_t *)(mock_jump_data + sizeof(mock_jump_data) - 16);
	v2_ptr[0] = 0; /* jump_tag_total */
	v2_ptr[1] = EC_RESET_FLAG_POWER_ON;
	v2_ptr[2] = 2;
	v2_ptr[3] = JUMP_DATA_MAGIC;

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	struct jump_data *jdata = get_test_jdata_ptr();
	zassert_equal(jdata->version, 2);
	zassert_equal(jdata->struct_size, sizeof(struct jump_data));
	zassert_equal(jdata->reserved0, 0);
}

ZTEST(jump_data, test_init_v2_with_tags)
{
	int tag_data_size = 4;
	int total_tag_size = sizeof(struct jump_tag) + tag_data_size;
	uint32_t *v2_ptr =
		(uint32_t *)(mock_jump_data + sizeof(mock_jump_data) - 16);
	v2_ptr[0] = total_tag_size;
	v2_ptr[1] = EC_RESET_FLAG_SOFT;
	v2_ptr[2] = 2;
	v2_ptr[3] = JUMP_DATA_MAGIC;

	struct jump_tag *tag =
		(struct jump_tag *)((uintptr_t)v2_ptr - total_tag_size);
	tag->tag = 0x1234;
	tag->data_size = tag_data_size;
	tag->data_version = 1;
	uint32_t *tag_data = (uint32_t *)(tag + 1);
	*tag_data = 0xcafebeef;

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	struct jump_data *jdata = get_test_jdata_ptr();
	zassert_equal(jdata->jump_tag_total, total_tag_size);

	struct jump_tag *new_tag =
		(struct jump_tag *)((uintptr_t)jdata - total_tag_size);
	zassert_equal(new_tag->tag, 0x1234);
	zassert_equal(*(uint32_t *)(new_tag + 1), 0xcafebeef);
}

ZTEST(jump_data, test_init_v99_jump_data)
{
	int tag_data_size = 4;
	int total_tag_size = sizeof(struct jump_tag) + tag_data_size;

	uint8_t *prev_jdata_ptr =
		(uint8_t *)(mock_jump_data + sizeof(mock_jump_data) - 28);

	uint32_t *u32_ptr = (uint32_t *)prev_jdata_ptr;
	u32_ptr[0] = 0xaabbccdd; /* new_v99_field */
	u32_ptr[1] = 0; /* reserved0 */
	u32_ptr[2] = 28;
	u32_ptr[3] = total_tag_size;
	u32_ptr[4] = EC_RESET_FLAG_HARD;
	u32_ptr[5] = 99; /* version */
	u32_ptr[6] = JUMP_DATA_MAGIC;

	struct jump_tag *tag =
		(struct jump_tag *)(prev_jdata_ptr - total_tag_size);
	tag->tag = 0x5678;
	tag->data_size = tag_data_size;
	tag->data_version = 2;
	*(uint32_t *)(tag + 1) = 0xdeadbeef;

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	struct jump_data *jdata = get_test_jdata_ptr();

	zassert_equal(jdata->version, 99);
	zassert_equal(jdata->jump_tag_total, total_tag_size);

	struct jump_tag *new_tag =
		(struct jump_tag *)((uintptr_t)jdata - total_tag_size);
	zassert_equal(new_tag->tag, 0x5678);
	zassert_equal(*(uint32_t *)(new_tag + 1), 0xdeadbeef);
}

ZTEST(jump_data, test_init_out_of_space)
{
	struct jump_data *jdata = get_test_jdata_ptr();
	jdata->magic = JUMP_DATA_MAGIC;
	jdata->version = 3;
	jdata->struct_size = sizeof(struct jump_data);

	jdata->jump_tag_total =
		(uintptr_t)jdata - (uintptr_t)mock_jump_data + 1;

	system_common_pre_init();

	zassert_equal(jdata->magic, 0);
	zassert_equal(jdata->version, 0);
	/* JUMP_DATA_MAGIC was present, so sysjump happened even if tags were
	 * dropped */
	zassert_equal(system_jumped_to_this_image(), 1);
}

ZTEST(jump_data, test_init_with_panic_data)
{
	struct panic_data *pdata = get_panic_data_write();
	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = sizeof(struct panic_data);

	struct jump_data *expected_jdata =
		(struct jump_data *)((uintptr_t)pdata -
				     sizeof(struct jump_data));

	zassert_equal((uintptr_t)get_jump_data(), (uintptr_t)expected_jdata);

	if ((uintptr_t)expected_jdata < JUMP_DATA_MIN_ADDRESS) {
		return;
	}

	expected_jdata->magic = JUMP_DATA_MAGIC;
	expected_jdata->version = 3;
	expected_jdata->struct_size = sizeof(struct jump_data);
	expected_jdata->jump_tag_total = 0;
	expected_jdata->reset_flags = EC_RESET_FLAG_POWER_ON;

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	zassert_equal(expected_jdata->magic, 0);
	zassert_equal(system_usable_ram_end(), (uintptr_t)expected_jdata);
}

ZTEST(jump_data, test_init_corrupted_jump_tag_total)
{
	struct jump_data *jdata = get_test_jdata_ptr();

	jdata->magic = JUMP_DATA_MAGIC;
	jdata->version = 3;
	jdata->struct_size = sizeof(struct jump_data);
	jdata->jump_tag_total = -100; /* Negative total! */
	jdata->reset_flags = EC_RESET_FLAG_POWER_ON;

	/* Should be safely caught and cleared */
	system_common_pre_init();

	zassert_equal(jdata->magic, 0);
	zassert_equal(system_jumped_to_this_image(), 1);
}

ZTEST(jump_data, test_init_corrupted_struct_size)
{
	struct jump_data *jdata = get_test_jdata_ptr();

	jdata->magic = JUMP_DATA_MAGIC;
	jdata->version = 3;
	jdata->struct_size = CONFIG_PLATFORM_EC_PRESERVED_END_OF_RAM_SIZE + 4;
	jdata->jump_tag_total = 8;
	jdata->reset_flags = EC_RESET_FLAG_POWER_ON;

	/* Should be safely caught and cleared */
	system_common_pre_init();

	zassert_equal(jdata->magic, 0);
	zassert_equal(system_jumped_to_this_image(), 1);
}

ZTEST_SUITE(jump_data, NULL, NULL, jump_data_before, NULL, NULL);
