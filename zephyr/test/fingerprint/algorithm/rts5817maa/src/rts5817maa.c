/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <fingerprint/fingerprint_alg.h>
#include <zephyr/drivers/fingerprint/fingerprint_rts5817maa_private.h>

DEFINE_FFF_GLOBALS;

/* Mocks for the private RTS5817MAA algorithm library. */
DECLARE_FAKE_VALUE_FUNC(rts_fp_return_t, rts_bio_algorithm_init);
DECLARE_FAKE_VOID_FUNC(rts_bio_algorithm_exit);
DECLARE_FAKE_VALUE_FUNC(rts_fp_return_t, rts_bio_enroll_start);
DECLARE_FAKE_VALUE_FUNC(rts_fp_return_t, rts_bio_enroll_step,
			const uint8_t *const, int *);
DECLARE_FAKE_VOID_FUNC(rts_bio_enroll_finish, void *);
DECLARE_FAKE_VALUE_FUNC(rts_fp_return_t, rts_bio_match, void *, uint32_t,
			const uint8_t *const, bool, int32_t *, uint32_t *);

DEFINE_FAKE_VALUE_FUNC(rts_fp_return_t, rts_bio_algorithm_init);
DEFINE_FAKE_VOID_FUNC(rts_bio_algorithm_exit);
DEFINE_FAKE_VALUE_FUNC(rts_fp_return_t, rts_bio_enroll_start);
DEFINE_FAKE_VALUE_FUNC(rts_fp_return_t, rts_bio_enroll_step,
		       const uint8_t *const, int *);
DEFINE_FAKE_VOID_FUNC(rts_bio_enroll_finish, void *);
DEFINE_FAKE_VALUE_FUNC(rts_fp_return_t, rts_bio_match, void *, uint32_t,
		       const uint8_t *const, bool, int32_t *, uint32_t *);

#define TEST_IMAGE_SIZE 16
#define TEST_TEMPL_SIZE 64

extern const struct fingerprint_algorithm rts5817maa_algorithm;
extern const rts5817maa_bio_info_t rts_bio_info;

struct rts5817maa_alg_fixture {
	const struct fingerprint_algorithm *alg;
	uint8_t image[TEST_IMAGE_SIZE];
	uint8_t templ[TEST_TEMPL_SIZE];
	bool is_initialized;
};

static rts_fp_return_t custom_enroll_step(const uint8_t *const image,
					  int *completion)
{
	if (completion) {
		*completion = 42;
	}

	return RTS_FP_SUCCESS;
}

static int fixture_alg_init(struct rts5817maa_alg_fixture *fixture)
{
	int res = fingerprint_algorithm_init(fixture->alg);

	if (res == 0) {
		fixture->is_initialized = true;
	}

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
	RESET_FAKE(rts_bio_enroll_start);
	RESET_FAKE(rts_bio_enroll_step);
	RESET_FAKE(rts_bio_enroll_finish);
	RESET_FAKE(rts_bio_match);

	rts_bio_algorithm_init_fake.return_val = RTS_FP_SUCCESS;
	rts_bio_enroll_start_fake.return_val = RTS_FP_SUCCESS;
	rts_bio_enroll_step_fake.return_val = RTS_FP_SUCCESS;
	rts_bio_match_fake.return_val = RTS_FP_SUCCESS;

	memset(fixture->image, 0, sizeof(fixture->image));
	memset(fixture->templ, 0, sizeof(fixture->templ));
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

ZTEST_F(rts5817maa_alg, test_enroll_start_success)
{
	zassert_ok(fixture_alg_init(fixture));

	zassert_ok(fingerprint_enroll_start(fixture->alg));
	zassert_equal(rts_bio_enroll_start_fake.call_count, 1);
}

ZTEST_F(rts5817maa_alg, test_enroll_start_failure)
{
	zassert_ok(fixture_alg_init(fixture));

	rts_bio_enroll_start_fake.return_val = RTS_ERROR_NOT_ENOUGH_MEMORY;

	zassert_equal(fingerprint_enroll_start(fixture->alg),
		      RTS_ERROR_NOT_ENOUGH_MEMORY);
}

ZTEST_F(rts5817maa_alg, test_enroll_step_success)
{
	int completion = 0;

	rts_bio_enroll_step_fake.custom_fake = custom_enroll_step;

	zassert_ok(fixture_alg_init(fixture));
	zassert_ok(fingerprint_enroll_start(fixture->alg));

	zassert_equal(
		fingerprint_enroll_step(fixture->alg, fixture->image,
					&completion),
		FP_ENROLLMENT_RESULT_OK);
	zassert_equal(completion, 42);
	zassert_equal_ptr(rts_bio_enroll_step_fake.arg0_val, fixture->image);
	zassert_equal_ptr(rts_bio_enroll_step_fake.arg1_val, &completion);
}

ZTEST_F(rts5817maa_alg, test_enroll_step_immobile)
{
	zassert_ok(fixture_alg_init(fixture));

	rts_bio_enroll_step_fake.return_val = RTS_FP_IMMOBILE;

	zassert_equal(fingerprint_enroll_step(fixture->alg, fixture->image,
					      NULL),
		      FP_ENROLLMENT_RESULT_IMMOBILE);
}

ZTEST_F(rts5817maa_alg, test_enroll_step_poor_quality)
{
	zassert_ok(fixture_alg_init(fixture));

	rts_bio_enroll_step_fake.return_val = RTS_FP_POOR_QUALITY;

	zassert_equal(fingerprint_enroll_step(fixture->alg, fixture->image,
					      NULL),
		      FP_ENROLLMENT_RESULT_LOW_QUALITY);
}

ZTEST_F(rts5817maa_alg, test_enroll_step_low_coverage)
{
	zassert_ok(fixture_alg_init(fixture));

	rts_bio_enroll_step_fake.return_val = RTS_FP_TOO_HIGH;
	zassert_equal(fingerprint_enroll_step(fixture->alg, fixture->image,
					      NULL),
		      FP_ENROLLMENT_RESULT_LOW_COVERAGE);

	rts_bio_enroll_step_fake.return_val = RTS_FP_TOO_LOW;
	zassert_equal(fingerprint_enroll_step(fixture->alg, fixture->image,
					      NULL),
		      FP_ENROLLMENT_RESULT_LOW_COVERAGE);

	rts_bio_enroll_step_fake.return_val = RTS_FP_TOO_LEFT;
	zassert_equal(fingerprint_enroll_step(fixture->alg, fixture->image,
					      NULL),
		      FP_ENROLLMENT_RESULT_LOW_COVERAGE);

	rts_bio_enroll_step_fake.return_val = RTS_FP_TOO_RIGHT;
	zassert_equal(fingerprint_enroll_step(fixture->alg, fixture->image,
					      NULL),
		      FP_ENROLLMENT_RESULT_LOW_COVERAGE);
}

ZTEST_F(rts5817maa_alg, test_enroll_step_unmapped_error)
{
	zassert_ok(fixture_alg_init(fixture));

	rts_bio_enroll_step_fake.return_val = RTS_FP_INTERNAL_ERROR;

	zassert_equal(fingerprint_enroll_step(fixture->alg, fixture->image,
					      NULL),
		      -EINVAL);
}

ZTEST_F(rts5817maa_alg, test_enroll_finish_success)
{
	zassert_ok(fixture_alg_init(fixture));
	zassert_ok(fingerprint_enroll_start(fixture->alg));

	zassert_ok(fingerprint_enroll_finish(fixture->alg, fixture->templ));
	zassert_equal(rts_bio_enroll_finish_fake.call_count, 1);
	zassert_equal_ptr(rts_bio_enroll_finish_fake.arg0_val,
			  fixture->templ);
}

ZTEST_SUITE(rts5817maa_alg, NULL, rts5817maa_alg_setup, rts5817maa_alg_before,
	    rts5817maa_alg_after, NULL);
