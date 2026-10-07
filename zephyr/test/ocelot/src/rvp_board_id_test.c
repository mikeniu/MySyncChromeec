/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "system.h"

#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(rvp_model_id, LOG_LEVEL_DBG);

FAKE_VALUE_FUNC(int, cbi_get_sku_id, uint32_t *);

extern int rvp_model_id;

static uint32_t fake_sku_id;

static int cbi_get_sku_id_custom_fake(uint32_t *sku_id)
{
	*sku_id = fake_sku_id;
	return EC_SUCCESS;
}

static void rvp_board_id_before(void *fixture)
{
	ARG_UNUSED(fixture);

	rvp_model_id = -1;
	fake_sku_id = 0;
	RESET_FAKE(cbi_get_sku_id);
}

ZTEST_SUITE(ocelot_rvp_board_id, NULL, NULL, rvp_board_id_before, NULL, NULL);

ZTEST(ocelot_rvp_board_id, test_cbi_error)
{
	cbi_get_sku_id_fake.return_val = EC_ERROR_UNKNOWN;

	zassert_equal(board_get_version(), -1);
	zassert_equal(cbi_get_sku_id_fake.call_count, 1);
}

ZTEST(ocelot_rvp_board_id, test_lp5x_skus)
{
	static const uint32_t lp5x_skus[] = {
		1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12,
	};

	cbi_get_sku_id_fake.custom_fake = cbi_get_sku_id_custom_fake;

	for (size_t i = 0; i < ARRAY_SIZE(lp5x_skus); i++) {
		rvp_model_id = -1;
		fake_sku_id = lp5x_skus[i];

		zassert_equal(board_get_version(), 33,
			      "Expected board ID 33 for SKU %u", lp5x_skus[i]);
	}
}

ZTEST(ocelot_rvp_board_id, test_ddr5_sku)
{
	cbi_get_sku_id_fake.custom_fake = cbi_get_sku_id_custom_fake;
	fake_sku_id = 4;

	zassert_equal(board_get_version(), 32);
}

ZTEST(ocelot_rvp_board_id, test_unknown_sku)
{
	static const uint32_t unknown_skus[] = { 0, 13, 0xff };

	cbi_get_sku_id_fake.custom_fake = cbi_get_sku_id_custom_fake;

	for (size_t i = 0; i < ARRAY_SIZE(unknown_skus); i++) {
		rvp_model_id = -1;
		fake_sku_id = unknown_skus[i];

		zassert_equal(board_get_version(), -1,
			      "Expected -1 for unknown SKU %u",
			      unknown_skus[i]);
	}
}

ZTEST(ocelot_rvp_board_id, test_cached_model_id)
{
	cbi_get_sku_id_fake.custom_fake = cbi_get_sku_id_custom_fake;
	fake_sku_id = 10;

	zassert_equal(board_get_version(), 33);
	zassert_equal(cbi_get_sku_id_fake.call_count, 1);

	/* Second call should return the cached value without reading CBI */
	fake_sku_id = 4;
	zassert_equal(board_get_version(), 33);
	zassert_equal(cbi_get_sku_id_fake.call_count, 1);
}
