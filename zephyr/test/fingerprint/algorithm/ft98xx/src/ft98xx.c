/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ft98xx_algo_mocks.h"

#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <fingerprint/fingerprint_alg.h>
#include <subsys/fingerprint/alg/ft98xx_bio_alg.h>

DEFINE_FFF_GLOBALS;

/* Define fake function instances matching private header signatures */
DEFINE_FAKE_VALUE_FUNC(uint16_t, ft_sensor_query_rows);
DEFINE_FAKE_VALUE_FUNC(uint16_t, ft_sensor_query_cols);

DEFINE_FAKE_VALUE_FUNC(int, focal_algo_set_buffer, uint16_t *, uint8_t *,
		       uint8_t *);
DEFINE_FAKE_VALUE_FUNC(int, focal_algo_init, algo_param_t);
DEFINE_FAKE_VALUE_FUNC(int, focal_algo_deinit);
DEFINE_FAKE_VALUE_FUNC(int, focal_algo_get_finger_detailed_info, int *, int *,
		       int *);
DEFINE_FAKE_VOID_FUNC(focal_algo_version, uint8_t *);

DEFINE_FAKE_VALUE_FUNC(int, focal_algo_enroll_start);
DEFINE_FAKE_VALUE_FUNC(int, focal_algo_get_feature, uint8_t *, uint8_t *,
		       int *);
DEFINE_FAKE_VALUE_FUNC(int, focal_algo_enroll_step, uint8_t *, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, focal_algo_enroll_finish, uint8_t *, uint32_t *);
DEFINE_FAKE_VALUE_FUNC(int, focal_algo_enroll_cancel);

DEFINE_FAKE_VALUE_FUNC(int, focal_algo_match, uint8_t *, uint8_t *, uint8_t *);
DEFINE_FAKE_VALUE_FUNC(int, focal_algo_anti_spoofing, uint16_t *);
DEFINE_FAKE_VALUE_FUNC(int, focal_algo_update_template_by_feature, uint8_t *,
		       uint8_t *);

/* Define fixture structure globally at file scope for ZTEST_F */
struct ft98xx_bio_alg_fixture {
	const struct fingerprint_algorithm *alg;
	uint8_t fake_image[FT_RAW_SIZE + 100];
};

static int custom_get_finger_detailed_info(int *finger_size, int *subtpl_size,
					   int *head_size)
{
	if (finger_size)
		*finger_size = 1024;
	if (subtpl_size)
		*subtpl_size = 64;
	if (head_size)
		*head_size = 128;
	return 0;
}

extern const struct fingerprint_algorithm ft98xx_algorithm;

static void *ft98xx_alg_setup(void)
{
	static struct ft98xx_bio_alg_fixture fixture;

	/* Referencing ft98xx_algorithm directly prevents linker stripping */
	fixture.alg = &ft98xx_algorithm;
	zassert_not_null(fixture.alg);

	return &fixture;
}

static void ft98xx_alg_before(void *f)
{
	struct ft98xx_bio_alg_fixture *fixture = f;

	/* Reset algorithm state to prevent state leakage between tests */
	if (fixture->alg && fixture->alg->data) {
		memset(fixture->alg->data, 0, sizeof(struct ft_libfp_data));
	}

	/* Reset individual FFF mocks */
	RESET_FAKE(ft_sensor_query_rows);
	RESET_FAKE(ft_sensor_query_cols);
	RESET_FAKE(focal_algo_set_buffer);
	RESET_FAKE(focal_algo_init);
	RESET_FAKE(focal_algo_deinit);
	RESET_FAKE(focal_algo_get_finger_detailed_info);
	RESET_FAKE(focal_algo_version);
	RESET_FAKE(focal_algo_enroll_start);
	RESET_FAKE(focal_algo_get_feature);
	RESET_FAKE(focal_algo_enroll_step);
	RESET_FAKE(focal_algo_enroll_finish);
	RESET_FAKE(focal_algo_enroll_cancel);
	RESET_FAKE(focal_algo_match);
	RESET_FAKE(focal_algo_anti_spoofing);
	RESET_FAKE(focal_algo_update_template_by_feature);

	/* Default mock returns. */
	ft_sensor_query_rows_fake.return_val = 80;
	ft_sensor_query_cols_fake.return_val = 80;
	focal_algo_set_buffer_fake.return_val = 0;
	focal_algo_init_fake.return_val = 0;
	focal_algo_get_finger_detailed_info_fake.custom_fake =
		custom_get_finger_detailed_info;
}

ZTEST_F(ft98xx_bio_alg, test_enroll_step_uninitialized_session_fails)
{
	int completion = -1;

	zassert_ok(fingerprint_algorithm_init(fixture->alg));

	int res = fingerprint_enroll_step(fixture->alg, fixture->fake_image,
					  &completion);

	zassert_equal(res, FP_ENROLLMENT_RESULT_INTERNAL_ERROR,
		      "Expected INTERNAL_ERROR when remain == 0");
	zassert_equal(completion, 0, "Completion should be set to 0 on error");
	zassert_equal(
		focal_algo_get_feature_fake.call_count, 0,
		"Should not extract features if session is uninitialized");
}

ZTEST_F(ft98xx_bio_alg, test_enroll_step_get_feature_failure)
{
	int completion = 0;

	zassert_ok(fingerprint_algorithm_init(fixture->alg));
	zassert_ok(fingerprint_enroll_start(fixture->alg));

	focal_algo_get_feature_fake.return_val = -1;

	int res = fingerprint_enroll_step(fixture->alg, fixture->fake_image,
					  &completion);

	zassert_equal(
		res, FP_ENROLLMENT_RESULT_LOW_QUALITY,
		"Expected LOW_QUALITY result when feature extraction fails");
	zassert_equal(
		focal_algo_enroll_step_fake.call_count, 0,
		"Should not proceed to enroll step if feature extraction failed");
	zassert_equal(completion, 0, "Completion percentage should remain 0%");
}

ZTEST_F(ft98xx_bio_alg, test_enroll_step_algo_step_failure)
{
	int completion = 0;

	zassert_ok(fingerprint_algorithm_init(fixture->alg));
	zassert_ok(fingerprint_enroll_start(fixture->alg));

	focal_algo_get_feature_fake.return_val = 0;
	focal_algo_enroll_step_fake.return_val = -2;

	int res = fingerprint_enroll_step(fixture->alg, fixture->fake_image,
					  &completion);

	zassert_equal(res, FP_ENROLLMENT_RESULT_IMMOBILE,
		      "Expected IMMOBILE result when step fails");
	zassert_equal(
		completion, 0,
		"Completion should remain 0% because remain counter was not decremented");
}

ZTEST_F(ft98xx_bio_alg, test_enroll_step_success_and_progress)
{
	int completion = 0;
	const int total_samples = SINGLE_FINGER_ENROLL_NUM; /* 12 */

	zassert_ok(fingerprint_algorithm_init(fixture->alg));
	zassert_ok(fingerprint_enroll_start(fixture->alg));

	focal_algo_get_feature_fake.return_val = 0;
	focal_algo_enroll_step_fake.return_val = 0;

	/* Step 1 */
	int res = fingerprint_enroll_step(fixture->alg, fixture->fake_image,
					  &completion);
	zassert_equal(res, FP_ENROLLMENT_RESULT_OK);
	zassert_equal(focal_algo_enroll_step_fake.arg1_val, 0,
		      "First step index should be 0");
	zassert_equal(completion, (1 * 100) / total_samples,
		      "Completion should be ~8%% after 1 step");

	/* Step 2 */
	res = fingerprint_enroll_step(fixture->alg, fixture->fake_image,
				      &completion);
	zassert_equal(res, FP_ENROLLMENT_RESULT_OK);
	zassert_equal(focal_algo_enroll_step_fake.arg1_val, 1,
		      "Second step index should be 1");
	zassert_equal(completion, (2 * 100) / total_samples,
		      "Completion should be ~16%% after 2 steps");
}

ZTEST_F(ft98xx_bio_alg, test_full_enrollment_lifecycle)
{
	int completion = 0;
	uint8_t template_buf[1024] = { 0 };

	zassert_ok(fingerprint_algorithm_init(fixture->alg));
	zassert_ok(fingerprint_enroll_start(fixture->alg));

	focal_algo_get_feature_fake.return_val = 0;
	focal_algo_enroll_step_fake.return_val = 0;

	for (int i = 0; i < SINGLE_FINGER_ENROLL_NUM; i++) {
		int res = fingerprint_enroll_step(
			fixture->alg, fixture->fake_image, &completion);
		zassert_equal(res, FP_ENROLLMENT_RESULT_OK);
	}

	zassert_equal(completion, 100, "Completion should reach 100%");

	zassert_ok(fingerprint_enroll_finish(fixture->alg, template_buf));
	zassert_equal(focal_algo_enroll_finish_fake.call_count, 1);
}

ZTEST_F(ft98xx_bio_alg, test_enroll_cancel)
{
	zassert_ok(fingerprint_algorithm_init(fixture->alg));
	zassert_ok(fingerprint_enroll_start(fixture->alg));

	zassert_ok(fingerprint_enroll_finish(fixture->alg, NULL));
	zassert_equal(focal_algo_enroll_cancel_fake.call_count, 1);
}

ZTEST_SUITE(ft98xx_bio_alg, NULL, ft98xx_alg_setup, ft98xx_alg_before, NULL,
	    NULL);
