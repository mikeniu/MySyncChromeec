/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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

/* Test that V1 jump data is inflated to V3 jump data */
ZTEST(panic_output_jump_data, test_jump_data_v1_to_v3)
{
	struct jump_data_v1 jdata_init = {
		.magic = JUMP_DATA_MAGIC,
		.version = 1,
		.reset_flags = 0xAABBCCDD,
	};
	struct jump_data jdata_expected = {
		.magic = 0,
		.version = 3,
		.reset_flags = jdata_init.reset_flags,
		.jump_tag_total = 0,
		.struct_size = sizeof(struct jump_data),
		.reserved0 = 0,
	};

	*GET_JUMP_DATA_PTR(struct jump_data_v1, 0) = jdata_init;

	zassert_mem_equal(&jdata_expected, get_jump_data(),
			  sizeof(struct jump_data));
}

/* Test that V2 jump data is inflated to V3 jump data */
ZTEST(panic_output_jump_data, test_jump_data_v2_to_v3)
{
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
			.reserved0 = 8,
		},
	};

	struct jump_data *jdata;
	*GET_JUMP_DATA_PTR(struct jump_data_v2_plus_tags, 0) = jdata_init;

	zassert_false(system_jumped_to_this_image());

	jdata = get_jump_data();

	zassert_true(system_jumped_to_this_image());

	zassert_mem_equal(&jdata_expected,
			  (uint8_t *)jdata - jdata->jump_tag_total,
			  sizeof(jdata_expected));
}

/* Test that fake V99 jump data is deflated to V3 jump data */
ZTEST(panic_output_jump_data, test_jump_data_v99_to_v3)
{
	struct jump_data_v99_plus_tags jdata_init = {
		/* Arbitrary jump tag data */
		.tag_data = {1, 2, 3, 4, 5, 6, 7, 8},
		.jdata = {
			.extra_field = 0x12345678,
			.magic = JUMP_DATA_MAGIC,
			.version = 99,
			.reset_flags = 0xAABBCCDD,
			.struct_size = sizeof(jdata_init.jdata),
			.jump_tag_total = sizeof(jdata_init.tag_data),
			.reserved0 = 0xff,
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

	struct jump_data *jdata;
	*GET_JUMP_DATA_PTR(struct jump_data_v99_plus_tags, 0) = jdata_init;

	zassert_false(system_jumped_to_this_image());

	jdata = get_jump_data();

	zassert_true(system_jumped_to_this_image());

	zassert_equal(jdata->magic, 0);
	zassert_equal(jdata->version, 3);
	zassert_equal(jdata->reset_flags, jdata_expected.jdata.reset_flags);
	zassert_equal(jdata->jump_tag_total,
		      jdata_expected.jdata.jump_tag_total);
	zassert_equal(jdata->struct_size, sizeof(struct jump_data));
	zassert_equal(jdata->reserved0, 0xFF);

	zassert_mem_equal(&jdata_expected,
			  (uint8_t *)jdata - jdata->jump_tag_total,
			  sizeof(jdata_expected));
}

/* Test that V3 jump data is initialized correctly */
ZTEST(panic_output_jump_data, test_jump_data_v3_init)
{
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
			.reset_flags = 0xAABBCCDD,
			.jump_tag_total = sizeof(jdata_init.tag_data),
			.struct_size = sizeof(jdata_init.jdata),
			.reserved0 = 0xFF,
		},
	};

	struct jump_data *jdata;

	*GET_JUMP_DATA_PTR(struct jump_data_v3_plus_tags, 0) = jdata_init;

	zassert_false(system_jumped_to_this_image());

	jdata = get_jump_data();

	zassert_true(system_jumped_to_this_image());

	zassert_mem_equal(&jdata_expected,
			  (uint8_t *)jdata - jdata->jump_tag_total,
			  sizeof(jdata_expected));
}

/* Test that bad jump data is cleared */
ZTEST(panic_output_jump_data, test_jump_data_bad_magic)
{
	struct jump_data_v1 jdata_init = {
		.magic = 0,
		.version = 1,
		.reset_flags = 0xAABBCCDD,
	};
	struct jump_data jdata_expected = { 0 };
	struct jump_data *jdata;

	*GET_JUMP_DATA_PTR(struct jump_data_v1, 0) = jdata_init;

	zassert_false(system_jumped_to_this_image());

	jdata = get_jump_data();

	zassert_false(system_jumped_to_this_image());

	zassert_mem_equal(&jdata_expected, jdata, sizeof(jdata_expected));
}

/* Test that bad jump data is cleared */
ZTEST(panic_output_jump_data, test_jump_data_bad_version)
{
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
	struct jump_data *jdata;

	*GET_JUMP_DATA_PTR(struct jump_data_v3_plus_tags, 0) = jdata_init;

	zassert_false(system_jumped_to_this_image());

	jdata = get_jump_data();

	zassert_false(system_jumped_to_this_image());

	zassert_mem_equal(&jdata_expected, jdata, sizeof(jdata_expected));
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	memset(mock_jump_data, 0, sizeof(mock_jump_data));
	system_common_reset_state();
}

ZTEST_SUITE(panic_output_jump_data, drivers_predicate_post_main, NULL, reset,
	    reset, NULL);
