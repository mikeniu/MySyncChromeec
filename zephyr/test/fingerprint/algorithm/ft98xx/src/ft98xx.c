/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ft98xx_algo_mocks.h"

#include <errno.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <fingerprint/fingerprint_alg.h>
#include <subsys/fingerprint/alg/ft98xx_bio_alg.h>

DEFINE_FFF_GLOBALS;

/* Define fake function instances matching private header signatures. */
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

#define SENSOR_ROWS 80
#define SENSOR_COLS 80
#define SENSOR_FRAME_SIZE (SENSOR_ROWS * SENSOR_COLS)

/** Template header and sub-template size definitions (in bytes). */
#define FOCAL_TEMPLATE_HEADER_SIZE 128
#define FOCAL_TEMPLATE_SUBTEMPLATE_SIZE 64

#define FOCAL_ALGO_VERSION_MAX_LEN 32

/* Define fixture structure globally at file scope for ZTEST_F. */
struct ft98xx_bio_alg_fixture {
	const struct fingerprint_algorithm *alg;
	uint8_t fake_image[FT_RAW_SIZE + SENSOR_FRAME_SIZE];
	bool is_initialized;
};

/**
 * Helper to initialize algorithm and update fixture lifecycle status.
 */
static int fixture_alg_init(struct ft98xx_bio_alg_fixture *fixture)
{
	int res = fingerprint_algorithm_init(fixture->alg);

	if (res == 0) {
		fixture->is_initialized = true;
	}
	return res;
}

static int custom_get_finger_detailed_info(int *finger_size, int *subtpl_size,
					   int *head_size)
{
	if (finger_size)
		*finger_size = CONFIG_FP_ALGORITHM_TEMPLATE_SIZE;
	if (subtpl_size)
		*subtpl_size = FOCAL_TEMPLATE_SUBTEMPLATE_SIZE;
	if (head_size)
		*head_size = FOCAL_TEMPLATE_HEADER_SIZE;
	return 0;
}

static int custom_enroll_finish(uint8_t *tpl, uint32_t *size)
{
	if (size) {
		*size = CONFIG_FP_ALGORITHM_TEMPLATE_SIZE;
	}
	return 0;
}

static void custom_focal_algo_version(uint8_t *ver)
{
	if (ver) {
		snprintf((char *)ver, FOCAL_ALGO_VERSION_MAX_LEN, "v1.0.0");
	}
}

static int custom_get_finger_detailed_info_too_large(int *finger_size,
						     int *subtpl_size,
						     int *head_size)
{
	if (finger_size)
		*finger_size = CONFIG_FP_ALGORITHM_TEMPLATE_SIZE + 1;
	if (subtpl_size)
		*subtpl_size = 0;
	if (head_size)
		*head_size = 0;

	return 0;
}

static int custom_match_update_flag(uint8_t *feature, uint8_t *tpl,
				    uint8_t *update_flag)
{
	if (update_flag)
		*update_flag = 1;
	return 0;
}

extern const struct fingerprint_algorithm ft98xx_algorithm;

static void *ft98xx_alg_setup(void)
{
	static struct ft98xx_bio_alg_fixture fixture;

	/* Referencing ft98xx_algorithm directly prevents linker stripping. */
	fixture.alg = &ft98xx_algorithm;
	zassert_not_null(fixture.alg);

	return &fixture;
}

static void ft98xx_alg_before(void *f)
{
	struct ft98xx_bio_alg_fixture *fixture = f;

	fixture->is_initialized = false;

	/* Reset global FFF call history sequence tracker. */
	FFF_RESET_HISTORY();

	/* Reset algorithm state to prevent state leakage between tests. */
	if (fixture->alg && fixture->alg->data) {
		/*
		 * Safe to memset: ft_libfp_data is POD (scalar fields and
		 * pointers only, with no embedded Zephyr OS primitives like
		 * k_mutex or k_sem).
		 */
		memset(fixture->alg->data, 0, sizeof(struct ft_libfp_data));
	}

	/* Initialize image buffer to ensure deterministic memory state. */
	memset(fixture->fake_image, 0, sizeof(fixture->fake_image));

	/* Reset individual FFF mocks. */
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
	ft_sensor_query_rows_fake.return_val = SENSOR_ROWS;
	ft_sensor_query_cols_fake.return_val = SENSOR_COLS;
	focal_algo_set_buffer_fake.return_val = 0;
	focal_algo_init_fake.return_val = 0;
	focal_algo_deinit_fake.return_val = 0;
	focal_algo_enroll_start_fake.return_val = 0;
	focal_algo_get_feature_fake.return_val = 0;
	focal_algo_enroll_step_fake.return_val = 0;
	focal_algo_enroll_finish_fake.return_val = 0;
	focal_algo_enroll_cancel_fake.return_val = 0;
	focal_algo_match_fake.return_val = 0;
	focal_algo_anti_spoofing_fake.return_val = 0;
	focal_algo_update_template_by_feature_fake.return_val = 0;
	focal_algo_get_finger_detailed_info_fake.custom_fake =
		custom_get_finger_detailed_info;
	focal_algo_enroll_finish_fake.custom_fake = custom_enroll_finish;
}

static void ft98xx_alg_after(void *f)
{
	struct ft98xx_bio_alg_fixture *fixture = f;

	if (fixture->alg && fixture->is_initialized) {
		fingerprint_algorithm_exit(fixture->alg);
		fixture->is_initialized = false;
	}
}

ZTEST_F(ft98xx_bio_alg, test_init_success_and_version_query)
{
	focal_algo_version_fake.custom_fake = custom_focal_algo_version;

	zassert_ok(fixture_alg_init(fixture));

	zassert_equal(focal_algo_version_fake.call_count, 1);
	zassert_not_null(focal_algo_version_fake.arg0_val);
}

ZTEST_F(ft98xx_bio_alg, test_init_set_buffer_failure)
{
	focal_algo_set_buffer_fake.return_val = -1;
	zassert_equal(fingerprint_algorithm_init(fixture->alg), -EINVAL);
}

ZTEST_F(ft98xx_bio_alg, test_init_algo_init_failure)
{
	focal_algo_init_fake.return_val = -1;
	zassert_equal(fingerprint_algorithm_init(fixture->alg), -EINVAL);
}

ZTEST_F(ft98xx_bio_alg, test_init_template_size_exceeded)
{
	focal_algo_get_finger_detailed_info_fake.custom_fake =
		custom_get_finger_detailed_info_too_large;
	zassert_equal(fingerprint_algorithm_init(fixture->alg), -EINVAL);
}

ZTEST_F(ft98xx_bio_alg, test_exit_success)
{
	zassert_ok(fixture_alg_init(fixture));

	zassert_ok(fingerprint_algorithm_exit(fixture->alg));
	fixture->is_initialized = false;

	zassert_equal(focal_algo_deinit_fake.call_count, 1);
}

ZTEST_F(ft98xx_bio_alg, test_enroll_start_failure)
{
	zassert_ok(fixture_alg_init(fixture));

	focal_algo_enroll_start_fake.return_val = -1;

	int res = fingerprint_enroll_start(fixture->alg);
	/*
	 * TODO(b/543034271): ft98xx_enroll_start always returns 0 even when
	 * focal_algo_enroll_start fails.
	 */
	zassert_equal(res, 0, "Expected 0 when focal_algo_enroll_start fails.");
}

ZTEST_F(ft98xx_bio_alg, test_enroll_step_get_feature_failure)
{
	int completion = 0;

	zassert_ok(fixture_alg_init(fixture));

	zassert_ok(fingerprint_enroll_start(fixture->alg));

	focal_algo_get_feature_fake.return_val = -1;

	int res = fingerprint_enroll_step(fixture->alg, fixture->fake_image,
					  &completion);

	zassert_equal(
		res, FP_ENROLLMENT_RESULT_LOW_QUALITY,
		"Expected LOW_QUALITY result when feature extraction fails.");
	zassert_equal(
		focal_algo_enroll_step_fake.call_count, 0,
		"Should not proceed to enroll step if feature extraction failed.");
	zassert_equal(completion, 0, "Completion percentage should remain 0%.");
}

ZTEST_F(ft98xx_bio_alg, test_enroll_step_algo_step_failure)
{
	int completion = 0;

	zassert_ok(fixture_alg_init(fixture));

	zassert_ok(fingerprint_enroll_start(fixture->alg));

	focal_algo_get_feature_fake.return_val = 0;
	focal_algo_enroll_step_fake.return_val = -2;

	int res = fingerprint_enroll_step(fixture->alg, fixture->fake_image,
					  &completion);

	zassert_equal(res, FP_ENROLLMENT_RESULT_IMMOBILE,
		      "Expected IMMOBILE result when step fails.");
	zassert_equal(
		completion, 0,
		"Completion should remain 0% because remain counter was not decremented.");
}

ZTEST_F(ft98xx_bio_alg, test_enroll_step_null_completion_fails)
{
	zassert_ok(fingerprint_algorithm_init(fixture->alg));
	zassert_ok(fingerprint_enroll_start(fixture->alg));

	int res = fingerprint_enroll_step(fixture->alg, fixture->fake_image,
					  NULL);

	zassert_equal(res, -EINVAL,
		      "Expected -EINVAL when completion pointer is NULL.");
	zassert_equal(
		focal_algo_get_feature_fake.call_count, 0,
		"Should not extract features when completion pointer is NULL.");
	zassert_equal(
		focal_algo_enroll_step_fake.call_count, 0,
		"Should not call enroll step when completion pointer is NULL.");
}

ZTEST_F(ft98xx_bio_alg, test_enroll_step_success_and_progress)
{
	int completion = 0;
	const int total_samples = SINGLE_FINGER_ENROLL_NUM;

	zassert_ok(fixture_alg_init(fixture));

	zassert_ok(fingerprint_enroll_start(fixture->alg));

	focal_algo_get_feature_fake.return_val = 0;
	focal_algo_enroll_step_fake.return_val = 0;

	/* Step 1. */
	int res = fingerprint_enroll_step(fixture->alg, fixture->fake_image,
					  &completion);
	zassert_equal(res, FP_ENROLLMENT_RESULT_OK);
	zassert_equal(focal_algo_enroll_step_fake.arg1_val, 0,
		      "First step index should be 0.");
	zassert_equal(
		completion, (1 * 100) / total_samples,
		"Completion should reflect 1 out of total enrollment steps.");

	/* Step 2. */
	res = fingerprint_enroll_step(fixture->alg, fixture->fake_image,
				      &completion);
	zassert_equal(res, FP_ENROLLMENT_RESULT_OK);
	zassert_equal(focal_algo_enroll_step_fake.arg1_val, 1,
		      "Second step index should be 1.");
	zassert_equal(
		completion, (2 * 100) / total_samples,
		"Completion should reflect 2 out of total enrollment steps.");
}

ZTEST_F(ft98xx_bio_alg, test_full_enrollment_lifecycle)
{
	int completion = 0;
	uint8_t template_buf[CONFIG_FP_ALGORITHM_TEMPLATE_SIZE] = { 0 };

	zassert_ok(fixture_alg_init(fixture));

	zassert_ok(fingerprint_enroll_start(fixture->alg));

	focal_algo_get_feature_fake.return_val = 0;
	focal_algo_enroll_step_fake.return_val = 0;

	for (int i = 0; i < SINGLE_FINGER_ENROLL_NUM; i++) {
		int res = fingerprint_enroll_step(
			fixture->alg, fixture->fake_image, &completion);
		zassert_equal(res, FP_ENROLLMENT_RESULT_OK);
	}

	zassert_equal(completion, 100, "Completion should reach 100%.");

	zassert_ok(fingerprint_enroll_finish(fixture->alg, template_buf));
	zassert_equal(focal_algo_enroll_finish_fake.call_count, 1);
}

ZTEST_F(ft98xx_bio_alg, test_enroll_finish_failure)
{
	uint8_t template_buf[CONFIG_FP_ALGORITHM_TEMPLATE_SIZE] = { 0 };

	zassert_ok(fixture_alg_init(fixture));

	zassert_ok(fingerprint_enroll_start(fixture->alg));

	focal_algo_enroll_finish_fake.return_val = -1;

	int res = fingerprint_enroll_finish(fixture->alg, template_buf);
	zassert_equal(
		res, FP_ENROLLMENT_RESULT_OK,
		"Expected RESULT_OK when focal_algo_enroll_finish fails.");
}

ZTEST_F(ft98xx_bio_alg, test_enroll_cancel)
{
	zassert_ok(fixture_alg_init(fixture));

	zassert_ok(fingerprint_enroll_start(fixture->alg));

	/*
	 * The ChromeOS EC fingerprint algorithm subsystem API designates
	 * passing a NULL template pointer to enroll_finish() as the official
	 * way to cancel an ongoing enrollment session.
	 */
	zassert_ok(fingerprint_enroll_finish(fixture->alg, NULL));
	zassert_equal(focal_algo_enroll_cancel_fake.call_count, 1);
	zassert_equal(focal_algo_enroll_finish_fake.call_count, 0);
}

ZTEST_F(ft98xx_bio_alg, test_match_get_feature_failure)
{
	int32_t match_index = -1;
	uint32_t update_bitmap = 0;
	uint8_t template_buf[CONFIG_FP_ALGORITHM_TEMPLATE_SIZE] = { 0 };

	zassert_ok(fixture_alg_init(fixture));

	focal_algo_get_feature_fake.return_val = -1;

	int res = fingerprint_match(fixture->alg, template_buf, 1,
				    fixture->fake_image, false, &match_index,
				    &update_bitmap);

	zassert_equal(res, FP_MATCH_RESULT_NO_MATCH);
	zassert_equal(match_index, -1);
	zassert_equal(update_bitmap, 0);
}

ZTEST_F(ft98xx_bio_alg, test_match_success_no_update)
{
	int32_t match_index = -1;
	uint32_t update_bitmap = 0;
	uint8_t template_buf[CONFIG_FP_ALGORITHM_TEMPLATE_SIZE] = { 0 };

	zassert_ok(fixture_alg_init(fixture));

	focal_algo_get_feature_fake.return_val = 0;
	focal_algo_match_fake.return_val = 0; /* Match found */
	focal_algo_anti_spoofing_fake.return_val = 0; /* Genuine */

	int res = fingerprint_match(fixture->alg, template_buf, 1,
				    fixture->fake_image, false, &match_index,
				    &update_bitmap);

	zassert_equal(res, FP_MATCH_RESULT_MATCH);
	zassert_equal(match_index, 0);
	zassert_equal(update_bitmap, 0);

	zassert_equal(focal_algo_anti_spoofing_fake.call_count, 1,
		      "Anti-spoofing check must be performed on match.");
	zassert_equal(
		focal_algo_update_template_by_feature_fake.call_count, 0,
		"Template update must be skipped when template_update is false.");
}

ZTEST_F(ft98xx_bio_alg, test_match_anti_spoofing_failure)
{
	int32_t match_index = -1;
	uint32_t update_bitmap = 0;
	uint8_t template_buf[CONFIG_FP_ALGORITHM_TEMPLATE_SIZE] = { 0 };

	zassert_ok(fixture_alg_init(fixture));

	focal_algo_get_feature_fake.return_val = 0;
	focal_algo_match_fake.return_val = 0;
	focal_algo_anti_spoofing_fake.return_val = -1; /* Fake finger */

	int res = fingerprint_match(fixture->alg, template_buf, 1,
				    fixture->fake_image, true, &match_index,
				    &update_bitmap);

	zassert_equal(res, FP_MATCH_RESULT_NO_MATCH);
	zassert_equal(match_index, -1);
	zassert_equal(update_bitmap, 0);
}

ZTEST_F(ft98xx_bio_alg, test_match_success_with_template_update)
{
	int32_t match_index = -1;
	uint32_t update_bitmap = 0;
	uint8_t template_buf[CONFIG_FP_ALGORITHM_TEMPLATE_SIZE] = { 0 };

	zassert_ok(fixture_alg_init(fixture));

	focal_algo_get_feature_fake.return_val = 0;
	focal_algo_match_fake.custom_fake = custom_match_update_flag;
	focal_algo_anti_spoofing_fake.return_val = 0;
	focal_algo_update_template_by_feature_fake.return_val = 0;

	int res = fingerprint_match(fixture->alg, template_buf, 1,
				    fixture->fake_image, true, &match_index,
				    &update_bitmap);

	zassert_equal(res, FP_MATCH_RESULT_MATCH_UPDATED);
	zassert_equal(match_index, 0);
	zassert_equal(update_bitmap, BIT(0));
}

ZTEST_F(ft98xx_bio_alg, test_match_template_update_failure)
{
	int32_t match_index = -1;
	uint32_t update_bitmap = 0;
	uint8_t template_buf[CONFIG_FP_ALGORITHM_TEMPLATE_SIZE] = { 0 };

	zassert_ok(fixture_alg_init(fixture));

	focal_algo_get_feature_fake.return_val = 0;
	focal_algo_match_fake.custom_fake = custom_match_update_flag;
	focal_algo_anti_spoofing_fake.return_val = 0;
	focal_algo_update_template_by_feature_fake.return_val = -1; /* Update
								       failed */

	int res = fingerprint_match(fixture->alg, template_buf, 1,
				    fixture->fake_image, true, &match_index,
				    &update_bitmap);

	zassert_equal(res, FP_MATCH_RESULT_MATCH_UPDATE_FAILED);
	zassert_equal(match_index, 0);
	zassert_equal(update_bitmap, 0);
}

ZTEST_F(ft98xx_bio_alg, test_match_algo_match_failure)
{
	int32_t match_index = -1;
	uint32_t update_bitmap = 0;
	uint8_t template_buf[CONFIG_FP_ALGORITHM_TEMPLATE_SIZE] = { 0 };

	zassert_ok(fixture_alg_init(fixture));

	focal_algo_get_feature_fake.return_val = 0;
	focal_algo_match_fake.return_val = -1; /* Algorithm match error */

	int res = fingerprint_match(fixture->alg, template_buf, 1,
				    fixture->fake_image, false, &match_index,
				    &update_bitmap);

	zassert_equal(
		res, FP_MATCH_RESULT_NO_MATCH,
		"Expected NO_MATCH when focal_algo_match fails with generic error.");
	zassert_equal(match_index, -1);
	zassert_equal(update_bitmap, 0);
}

ZTEST_SUITE(ft98xx_bio_alg, NULL, ft98xx_alg_setup, ft98xx_alg_before,
	    ft98xx_alg_after, NULL);
