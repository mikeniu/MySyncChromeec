/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"
#include "panic_output_jump_data_utils.h"
#include "sysjump.h"
#include "system.h"
#include "test/drivers/test_state.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <sys/types.h>

ZTEST(panic_output_get_panic_data_write, test_existing_panic_data)
{
	struct panic_data *pdata_actual = test_get_panic_data_pointer();

	/* Pretend panic data exists by setting the magic header and setting its
	 * size.
	 */
	pdata_actual->magic = PANIC_DATA_MAGIC;
	pdata_actual->struct_size = CONFIG_PANIC_DATA_SIZE;

	/* Verify that pdata_ptr is returned */
	zassert_equal(pdata_actual, get_panic_data_write());
}

ZTEST(panic_output_get_panic_data_write, test_no_panic_data__no_jump_data)
{
	struct panic_data *pdata_actual = test_get_panic_data_pointer();
	struct panic_data pdata_expected = {
		.magic = PANIC_DATA_MAGIC,
		.struct_size = CONFIG_PANIC_DATA_SIZE,
	};

	/* Don't fill in any panic data, but add some fake data so we can ensure
	 * it gets reset to zero.
	 */
	pdata_actual->flags = 0xFF;

	/* Verify that pdata_ptr is returned */
	zassert_equal(pdata_actual, get_panic_data_write());

	/* Verify the pdata struct has correct fields filled out. */
	zassert_mem_equal(&pdata_expected, pdata_actual,
			  sizeof(struct panic_data));
}

/* Test that V1 jump data is moved correctly. */
ZTEST(panic_output_get_panic_data_write, test_no_panic_data__jump_data_v1)
{
	struct panic_data *pdata_actual = test_get_panic_data_pointer();
	struct jump_data_v1 jdata_init = {
		.magic = JUMP_DATA_MAGIC,
		.version = 1,
		.reset_flags = 0xAABBCCDD,
	};
	struct jump_data_v3_plus_tags jdata_expected = {
		.jdata = {
			.magic = 0,
			.version = 3,
			.reset_flags = jdata_init.reset_flags,
			.jump_tag_total = 0,
			.struct_size = sizeof(struct jump_data),
			.reserved0 = 0,
		},
	};

	/* Set up some jump data. Version 1 does not have any jump tags, only
	 * the magic, version number, and reset_flags so it is constant size.
	 */
	*GET_JUMP_DATA_PTR(struct jump_data_v1, 0) = jdata_init;

	/* Verify that pdata_ptr is returned */
	zassert_equal(pdata_actual, get_panic_data_write());

	/* Verify that jump data has been moved to its new location */
	ssize_t expected_move_delta = -1 * sizeof(struct panic_data);
	struct jump_data_v3_plus_tags *jdata_moved = GET_JUMP_DATA_PTR(
		struct jump_data_v3_plus_tags, expected_move_delta);

	zassert_mem_equal(&jdata_expected, jdata_moved,
			  sizeof(struct jump_data_v3_plus_tags));
}

/* Test that V2 jump data is moved correctly. */
ZTEST(panic_output_get_panic_data_write, test_no_panic_data__jump_data_v2)
{
	struct panic_data *pdata_actual = test_get_panic_data_pointer();
	struct jump_data_v2_plus_tags jdata_init = {
		/* Arbitrary jump tag data */
		.tag_data = {1, 2, 3, 4, 5, 6, 7, 8},
		.jdata = {
			.magic = JUMP_DATA_MAGIC,
			.version = 2,
			.reset_flags = 0xAABBCCDD,
			.jump_tag_total = sizeof(jdata_init.tag_data),
		},
	};
	struct jump_data_v3_plus_tags jdata_expected = {
		/* Arbitrary jump tag data */
		.tag_data = {1, 2, 3, 4, 5, 6, 7, 8},
		.jdata = {
			.magic = 0,
			.version = 3,
			.reset_flags = jdata_init.jdata.reset_flags,
			.jump_tag_total = jdata_init.jdata.jump_tag_total,
			.struct_size = sizeof(struct jump_data),
		},
	};

	/* Set up some jump data and preceding tags */
	*GET_JUMP_DATA_PTR(struct jump_data_v2_plus_tags, 0) = jdata_init;

	/* Verify that pdata_ptr is returned */
	zassert_equal(pdata_actual, get_panic_data_write());

	/* Verify that jump data and tags moved to their new location */
	ssize_t expected_move_delta = -1 * sizeof(struct panic_data);
	struct jump_data_v3_plus_tags *jdata_moved = GET_JUMP_DATA_PTR(
		struct jump_data_v3_plus_tags, expected_move_delta);

	zassert_mem_equal(&jdata_expected, jdata_moved, sizeof(jdata_expected));
}

/* Test that V3 jump data is moved correctly. */
ZTEST(panic_output_get_panic_data_write, test_no_panic_data__jump_data_v3)
{
	struct panic_data *pdata_actual = test_get_panic_data_pointer();
	struct jump_data_v3_plus_tags jdata_init = {
		/* Arbitrary jump tag data */
		.tag_data = {1, 2, 3, 4, 5, 6, 7, 8},
		.jdata = {
			.magic = JUMP_DATA_MAGIC,
			.version = 3,
			.reset_flags = 0xAABBCCDD,
			.jump_tag_total = sizeof(jdata_init.tag_data),
			.struct_size = sizeof(jdata_init.jdata),
			.reserved0 = 0xFF,
		},
	};
	struct jump_data_v3_plus_tags jdata_expected = {
		/* Arbitrary jump tag data */
		.tag_data = {1, 2, 3, 4, 5, 6, 7, 8},
		.jdata = {
			.magic = 0,
			.version = 3,
			.reset_flags = jdata_init.jdata.reset_flags,
			.jump_tag_total = jdata_init.jdata.jump_tag_total,
			.struct_size = sizeof(struct jump_data),
			.reserved0 = 0xFF,
		},
	};
	/* Set up some jump data and preceding tags */
	*GET_JUMP_DATA_PTR(struct jump_data_v3_plus_tags, 0) = jdata_init;

	/* Verify that pdata_ptr is returned */
	zassert_equal(pdata_actual, get_panic_data_write());

	/* Verify that jump data and tags moved to their new location */
	ssize_t expected_move_delta = -1 * sizeof(struct panic_data);
	struct jump_data_v3_plus_tags *jdata_moved = GET_JUMP_DATA_PTR(
		struct jump_data_v3_plus_tags, expected_move_delta);

	zassert_mem_equal(&jdata_expected, jdata_moved,
			  sizeof(struct jump_data_v3_plus_tags));
}

/* Test that bad jump data is handled correctly. */
ZTEST(panic_output_get_panic_data_write, test_no_panic_data__jump_data_bad)
{
	struct panic_data *pdata_actual = test_get_panic_data_pointer();
	struct jump_data_v3_plus_tags jdata_init = {
		/* Arbitrary jump tag data */
		.tag_data = {1, 2, 3, 4, 5, 6, 7, 8},
		.jdata = {
			.magic = JUMP_DATA_MAGIC,
			.version = 0,
			.reset_flags = 0xAABBCCDD,
			.jump_tag_total = sizeof(jdata_init.tag_data),
			.struct_size = sizeof(jdata_init.jdata),
			.reserved0 = 0xFF,
		},
	};
	struct jump_data jdata_expected = { 0 };

	/* Set up some jump data and preceding tags */
	*GET_JUMP_DATA_PTR(struct jump_data_v3_plus_tags, 0) = jdata_init;

	/* Verify that pdata_ptr is returned */
	zassert_equal(pdata_actual, get_panic_data_write());

	/* Verify that jump data and tags moved to their new location */
	ssize_t expected_move_delta = -1 * sizeof(struct panic_data);
	struct jump_data *jdata_moved =
		GET_JUMP_DATA_PTR(struct jump_data, expected_move_delta);

	zassert_mem_equal(&jdata_expected, jdata_moved, sizeof(jdata_expected));
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	struct panic_data *pdata = test_get_panic_data_pointer();

	memset(pdata, 0, sizeof(struct panic_data));
	memset(mock_jump_data, 0, sizeof(mock_jump_data));
	system_common_reset_state();
}

ZTEST_SUITE(panic_output_get_panic_data_write, drivers_predicate_post_main,
	    NULL, reset, reset, NULL);
