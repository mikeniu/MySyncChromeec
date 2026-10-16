/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <fingerprint/fingerprint_alg.h>
#include <zephyr/drivers/fingerprint/fingerprint_rts5817maa_private.h>

DEFINE_FFF_GLOBALS;

/* Mocks for the private RTS5817MAA algorithm library. */
DECLARE_FAKE_VALUE_FUNC(rts_fp_return_t, rts_bio_algorithm_init);
DECLARE_FAKE_VOID_FUNC(rts_bio_algorithm_exit);

DEFINE_FAKE_VALUE_FUNC(rts_fp_return_t, rts_bio_algorithm_init);
DEFINE_FAKE_VOID_FUNC(rts_bio_algorithm_exit);

extern const struct fingerprint_algorithm rts5817maa_algorithm;
extern const rts5817maa_bio_info_t rts_bio_info;

struct rts5817maa_alg_fixture {
	const struct fingerprint_algorithm *alg;
	bool is_initialized;
};

static int fixture_alg_init(struct rts5817maa_alg_fixture *fixture)
{
	int res = fingerprint_algorithm_init(fixture->alg);

	fixture->is_initialized = (res == 0);

	return res;
}

static void *rts5817maa_alg_setup(void)
{
	static struct rts5817maa_alg_fixture fixture;

	/* Referencing rts5817maa_algorithm directly prevents linker stripping. */
	fixture.alg = &rts5817maa_algorithm;
	zassert_not_null(fixture.alg);

	return &fixture;
}

static void rts5817maa_alg_before(void *f)
{
	struct rts5817maa_alg_fixture *fixture = f;

	fixture->is_initialized = false;

	FFF_RESET_HISTORY();

	RESET_FAKE(rts_bio_algorithm_init);
	RESET_FAKE(rts_bio_algorithm_exit);

	rts_bio_algorithm_init_fake.return_val = RTS_FP_SUCCESS;
}

static void rts5817maa_alg_after(void *f)
{
	struct rts5817maa_alg_fixture *fixture = f;

	if (fixture->is_initialized) {
		fingerprint_algorithm_exit(fixture->alg);
		fixture->is_initialized = false;
	}
}

ZTEST_F(rts5817maa_alg, test_template_size)
{
	zassert_equal(rts_bio_info.template_size,
		      CONFIG_FP_ALGORITHM_TEMPLATE_SIZE);
}

ZTEST_F(rts5817maa_alg, test_init_success)
{
	zassert_ok(fixture_alg_init(fixture));

	zassert_equal(rts_bio_algorithm_init_fake.call_count, 1);
}

ZTEST_F(rts5817maa_alg, test_init_failure)
{
	rts_bio_algorithm_init_fake.return_val = RTS_ERROR_UNKNOWN;

	zassert_equal(fingerprint_algorithm_init(fixture->alg),
		      RTS_ERROR_UNKNOWN);
	zassert_equal(rts_bio_algorithm_init_fake.call_count, 1);
}

ZTEST_F(rts5817maa_alg, test_exit_success)
{
	zassert_ok(fixture_alg_init(fixture));

	zassert_ok(fingerprint_algorithm_exit(fixture->alg));
	fixture->is_initialized = false;

	zassert_equal(rts_bio_algorithm_exit_fake.call_count, 1);
}

ZTEST_SUITE(rts5817maa_alg, NULL, rts5817maa_alg_setup, rts5817maa_alg_before,
	    rts5817maa_alg_after, NULL);
