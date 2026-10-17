/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor_test_utils.h"
#include "mock_fingerprint_algorithm.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>
#include <ec_commands.h>
#include <ec_tasks.h>
#include <fingerprint/fingerprint_alg.h>
#include <flash.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>
#include <rollback.h>
#include <rollback_private.h>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

namespace
{

std::array<uint8_t, IMAGE_SIZE> image_buffer;

FpSensorInfoBuffer test_info_buffer;

constexpr std::array<uint8_t, CONFIG_FP_ALGORITHM_TEMPLATE_SIZE>
	kExampleTemplate = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
			     0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
			     0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
			     0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20 };

constexpr std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES>
	kExamplePositiveMatchSecret = { 0xc8, 0x3a, 0x56, 0x56, 0xe7, 0x96,
					0x06, 0xc3, 0xb3, 0xed, 0x47, 0x20,
					0x7e, 0x60, 0xbd, 0x5e, 0xef, 0x6c,
					0xa8, 0x84, 0xf2, 0x71, 0x86, 0x1a,
					0xf2, 0xa3, 0x6b, 0xa8, 0x1a, 0x82,
					0x59, 0x45 };

/*
 * Encrypted template with metadata and positive match salt, for more
 * information please check comment in fpsensor_template.c
 */
constexpr std::array<uint8_t, FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE>
	kExampleTemplateEncrypted = {
		/*
		 * Encryption metadata.
		 *
		 * struct_version - 2 bytes
		 * reserved - 2 bytes
		 * nonce - 12 bytes
		 * encryption_salt - 16 bytes
		 * tag - 16 bytes
		 */
		0x04, 0x00, 0x00, 0x00, 0x94, 0x1e, 0xe3, 0x47, 0x31, 0x0b,
		0x89, 0x73, 0x1a, 0xeb, 0xa4, 0x45, 0x2b, 0x2e, 0x90, 0x58,
		0xfa, 0x25, 0x3e, 0x3b, 0x21, 0x35, 0x9a, 0x25, 0x79, 0x20,
		0xba, 0x60, 0x6b, 0x73, 0xb8, 0xac, 0x86, 0x6f, 0xe1, 0xbc,
		0x86, 0xca, 0xf6, 0x42, 0x25, 0x1f, 0xd1, 0x22,
		/* Encrypted template. */
		0xed, 0x2f, 0xb5, 0xf3, 0x9a, 0x7a, 0xfe, 0x09, 0x82, 0x69,
		0x9a, 0xd0, 0xa0, 0x60, 0x35, 0x15, 0x87, 0xdf, 0xea, 0xf7,
		0x8f, 0x4f, 0xdf, 0x5d, 0x7a, 0x93, 0xcf, 0x61, 0xad, 0xe6,
		0xc2, 0x3a,
		/* Encrypted positive match salt. */
		0x0f, 0x8d, 0xe0, 0x47, 0x69, 0x0f, 0xda, 0xea, 0xbc, 0xdc,
		0x96, 0x7d, 0x69, 0x19, 0xac, 0xe7
	};

int match_result;
int32_t finger_index;
uint32_t finger_updated_bitmap;

void load_example_template()
{
	FpFullTemplatePacket<ec_params_fp_template> params;
	params.header()->offset = 0;
	params.header()->size =
		FpTemplateV0Size{
			.size = static_cast<uint32_t>(
				kExampleTemplateEncrypted.size()),
			.commit = true,
		}
			.pack();
	std::ranges::copy(kExampleTemplateEncrypted, params.data().begin());
	zassert_ok(
		ec_cmd_fp_template(nullptr, params.header(), sizeof(params)));
}

void expect_match_mkbp_event(uint32_t expected_errcode, uint32_t expected_idx)
{
	const FpMkbpEvent fp_events =
		take_mkbp_fp_events(mkbp_send_event_fake, 2);
	zassert_true(fp_events.is_finger_down());
	zassert_true(fp_events.is_match());
	zassert_equal(fp_events.error_code(), expected_errcode);
	zassert_equal(fp_events.match_index(), expected_idx);
}

/**
 * @brief Helper to set the FP mode and simulate a finger touch to trigger a
 * match.
 */
void trigger_match_attempt(uint32_t mode)
{
	/* Switch mode to match and wait for task to switch. */
	zassert_equal(set_fp_mode(mode) & mode, mode,
		      "Failed to set all requested FP modes");

	/* Put finger on the sensor. */
	update_sim_state([](auto &s) {
		s.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	});

	/* Trigger the sensor callback and wait for processing. */
	fingerprint_run_callback(fp_sim);
	k_msleep(1);
}

int custom_match(const fingerprint_algorithm *const alg, void *templ,
		 uint32_t templ_count, const uint8_t *const image,
		 bool template_update, int32_t *match_index,
		 uint32_t *update_bitmap)
{
	*match_index = finger_index;
	*update_bitmap = finger_updated_bitmap;

	return match_result;
}

void *fpsensor_setup()
{
	init_test_rollback([] {
		/* Start shimmed tasks. */
		start_ec_tasks();
		k_msleep(100);
	});

	/* Set TPM seed. */
	set_default_tpm_seed();

	return nullptr;
}

void fpsensor_before(void *f)
{
	reset_sim_state();
	RESET_FAKE(mkbp_send_event);
	reset_mock_alg_fakes();

	match_result = 0;
	finger_index = 0;
	finger_updated_bitmap = 0;

	reset_test_fp_context();

	/* Clear MKBP events from previous tests. */
	(void)global_context.take_events();
}

} /* namespace */

ZTEST_USER(fpsensor_match, test_match_no_templates_mkbp_event)
{
	/* Trigger match with no templates loaded. */
	trigger_match_attempt(FP_MODE_MATCH);

	expect_match_mkbp_event(EC_MKBP_FP_ERR_MATCH_NO_TEMPLATES,
				FP_NO_SUCH_TEMPLATE & 0xF);
}

ZTEST_USER(fpsensor_match, test_match_no_templates_mode_cleared)
{
	/* Trigger match with no templates. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Confirm that capture mode is not enabled. */
	zassert_false(get_fp_mode() & FP_MODE_MATCH);
}

ZTEST_USER(fpsensor_match, test_match_correct_template_and_image)
{
	load_example_template();

	/*
	 * Use custom match function to check if template and scan passed to
	 * matching algorithm is correct.
	 */
	mock_alg_match_fake.custom_fake =
		[](const fingerprint_algorithm *const alg, void *templ,
		   uint32_t templ_count, const uint8_t *const image,
		   bool template_update, int32_t *match_index,
		   uint32_t *update_bitmap) -> int {
		zassert_true(template_update);
		zassert_equal(templ_count, 1);
		zassert_true(std::ranges::equal(
			std::span(static_cast<const uint8_t *>(templ),
				  kExampleTemplate.size()),
			kExampleTemplate));
		zassert_true(std::ranges::equal(
			std::span(image, image_buffer.size()), image_buffer));

		return 0;
	};

	/* Prepare image. */
	image_buffer.fill(1);

	/* Load image to simulator. */
	fingerprint_load_image(fp_sim, image_buffer.data(),
			       image_buffer.size());

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);
}

ZTEST_USER(fpsensor_match, test_match_no_match_mkbp_event)
{
	load_example_template();

	mock_alg_match_fake.return_val = FP_MATCH_RESULT_NO_MATCH;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	expect_match_mkbp_event(EC_MKBP_FP_ERR_MATCH_NO,
				FP_NO_SUCH_TEMPLATE & 0xF);
}

ZTEST_USER(fpsensor_match, test_match_no_template_update)
{
	load_example_template();

	/*
	 * Check if FPSENSOR requested match without template update.
	 */
	mock_alg_match_fake.custom_fake =
		[](const fingerprint_algorithm *const alg, void *templ,
		   uint32_t templ_count, const uint8_t *const image,
		   bool template_update, int32_t *match_index,
		   uint32_t *update_bitmap) -> int {
		zassert_false(template_update);
		zassert_equal(templ_count, 1);

		return 0;
	};

	/* Trigger match without template update. */
	trigger_match_attempt(FP_MODE_MATCH | FP_MODE_MATCH_NO_TEMPLATE_UPDATE);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);
}

ZTEST_USER(fpsensor_match, test_match_success_mkbp_event)
{
	load_example_template();

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	expect_match_mkbp_event(EC_MKBP_FP_ERR_MATCH_YES, 0);
}

ZTEST_USER(fpsensor_match, test_match_success_template_updated_mkbp_event)
{
	load_example_template();

	match_result = FP_MATCH_RESULT_MATCH_UPDATED;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	expect_match_mkbp_event(EC_MKBP_FP_ERR_MATCH_YES_UPDATED, 0);
}

ZTEST_USER(fpsensor_match, test_match_success_template_update_failed_mkbp_event)
{
	load_example_template();

	match_result = FP_MATCH_RESULT_MATCH_UPDATE_FAILED;
	finger_index = 0;
	finger_updated_bitmap = 0;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	expect_match_mkbp_event(EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED, 0);
}

ZTEST_USER(fpsensor_match, test_match_success_template_updated_dirty_template)
{
	load_example_template();

	match_result = FP_MATCH_RESULT_MATCH_UPDATED;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that dirty templates bitmap is correct. */
	zassert_ok(get_fp_info_v3(test_info_buffer));
	zassert_equal(test_info_buffer.header.template_info.template_dirty,
		      0x1);
}

ZTEST_USER(fpsensor_match,
	   test_match_success_template_update_failed_dirty_template)
{
	load_example_template();

	match_result = FP_MATCH_RESULT_MATCH_UPDATE_FAILED;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that dirty templates bitmap is correct. */
	zassert_ok(get_fp_info_v3(test_info_buffer));
	zassert_equal(test_info_buffer.header.template_info.template_dirty,
		      0x0);
}

ZTEST_USER(fpsensor_match, test_match_success_no_template_update_dirty_template)
{
	load_example_template();

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that dirty templates bitmap is correct. */
	zassert_ok(get_fp_info_v3(test_info_buffer));
	zassert_equal(test_info_buffer.header.template_info.template_dirty,
		      0x0);
}

ZTEST_USER(fpsensor_match, test_match_no_template_update_dirty_template)
{
	load_example_template();

	/* Return MATCH_YES but NOT updated because we requested no update. */
	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match without update. */
	trigger_match_attempt(FP_MODE_MATCH | FP_MODE_MATCH_NO_TEMPLATE_UPDATE);

	/* Confirm that dirty templates bitmap is correct (no templates dirty).
	 */
	zassert_ok(get_fp_info_v3(test_info_buffer));
	zassert_equal(test_info_buffer.header.template_info.template_dirty,
		      0x0);
}

ZTEST_USER(fpsensor_match,
	   test_match_success_read_positive_match_secret_allowed)
{
	ec_params_fp_read_match_secret secret_params;
	ec_response_fp_read_match_secret secret_response;

	load_example_template();

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Read positive match secret for matched template. */
	secret_params.fgr = 0;
	zassert_ok(ec_cmd_fp_read_match_secret(nullptr, &secret_params,
					       &secret_response));
	zassert_true(std::ranges::equal(secret_response.positive_match_secret,
					kExamplePositiveMatchSecret));
}

ZTEST_USER(fpsensor_match,
	   test_match_success_read_positive_match_secret_timeout)
{
	ec_params_fp_read_match_secret secret_params;
	ec_response_fp_read_match_secret secret_response;

	load_example_template();

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Wait at least 15 seconds for positive match secret to timeout. */
	k_msleep(15000);

	/* Confirm that we can't read positive match secret. */
	secret_params.fgr = 0;
	zassert_equal(EC_RES_TIMEOUT,
		      ec_cmd_fp_read_match_secret(nullptr, &secret_params,
						  &secret_response));
}

ZTEST_USER(fpsensor_match, test_match_success_read_positive_match_secret_twice)
{
	ec_params_fp_read_match_secret secret_params;
	ec_response_fp_read_match_secret secret_response;

	load_example_template();

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	secret_params.fgr = 0;

	/* Expect that we can read positive match secret for the first time. */
	zassert_ok(ec_cmd_fp_read_match_secret(nullptr, &secret_params,
					       &secret_response));

	/*
	 * Confirm that we can't read positive match secret for the second time.
	 */
	zassert_equal(EC_RES_TIMEOUT,
		      ec_cmd_fp_read_match_secret(nullptr, &secret_params,
						  &secret_response));
}

ZTEST_USER(fpsensor_match,
	   test_match_read_positive_match_secret_without_match_fails)
{
	ec_params_fp_read_match_secret secret_params;
	ec_response_fp_read_match_secret secret_response;

	load_example_template();

	mock_alg_match_fake.return_val = FP_MATCH_RESULT_NO_MATCH;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that we can't read positive match secret. */
	secret_params.fgr = 0;
	zassert_equal(EC_RES_TIMEOUT,
		      ec_cmd_fp_read_match_secret(nullptr, &secret_params,
						  &secret_response));
}

ZTEST_USER(fpsensor_match, test_match_error_no_positive_match_secret)
{
	ec_params_fp_read_match_secret secret_params;
	ec_response_fp_read_match_secret secret_response;

	load_example_template();

	/* Negative value means error. */
	mock_alg_match_fake.return_val = -1;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that we can't read positive match secret. */
	secret_params.fgr = 0;
	zassert_equal(EC_RES_TIMEOUT,
		      ec_cmd_fp_read_match_secret(nullptr, &secret_params,
						  &secret_response));
}

ZTEST_USER(fpsensor_match, test_match_error_mkbp_event)
{
	load_example_template();

	/* Negative value means error. */
	mock_alg_match_fake.return_val = -1;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	expect_match_mkbp_event(EC_MKBP_FP_ERR_MATCH_NO_INTERNAL,
				FP_NO_SUCH_TEMPLATE & 0xF);
}

ZTEST_USER(fpsensor_match, test_match_no_match_keeps_stats_valid)
{
	ec_response_fp_stats stats = { 0 };

	load_example_template();

	/* Mock a "No Match" result from the algorithm. */
	mock_alg_match_fake.return_val = FP_MATCH_RESULT_NO_MATCH;

	/* Trigger a match attempt. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Ensure the match algorithm was actually executed. */
	zassert_equal(mock_alg_match_fake.call_count, 1,
		      "Match algorithm was not called");

	/* Verify timestamps_invalid should not have FPSTATS_MATCHING_INV. */
	zassert_ok(ec_cmd_fp_stats(nullptr, &stats));
	zassert_false(
		stats.timestamps_invalid & FPSTATS_MATCHING_INV,
		"Matching stats should be valid even on a 'No Match' result");
}

ZTEST_USER(fpsensor_match, test_match_internal_error_sets_invalid_flag)
{
	ec_response_fp_stats stats = { 0 };

	load_example_template();

	/* Mock an internal error (Negative value means error). */
	mock_alg_match_fake.return_val = -1;

	/* Trigger a match attempt. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Ensure the match algorithm was actually executed. */
	zassert_equal(mock_alg_match_fake.call_count, 1,
		      "Match algorithm was not called");

	/* Verify timestamps_invalid should have FPSTATS_MATCHING_INV. */
	zassert_ok(ec_cmd_fp_stats(nullptr, &stats));
	zassert_true(stats.timestamps_invalid & FPSTATS_MATCHING_INV,
		     "Internal error must set the invalid flag");
}

ZTEST_USER(fpsensor_match, test_match_no_templates_sets_invalid)
{
	ec_response_fp_stats stats = { 0 };

	/*
	 * Ensure NO templates are loaded.
	 * (fpsensor_before resets the state, so we just don't call
	 * ec_cmd_fp_template)
	 */

	/* Trigger a match attempt. */
	trigger_match_attempt(FP_MODE_MATCH);

	/*
	 * Even if no match happened, the 'matching' attempt occurred
	 * but was invalid because of missing templates.
	 */
	zassert_ok(ec_cmd_fp_stats(nullptr, &stats));
	zassert_true(stats.timestamps_invalid & FPSTATS_MATCHING_INV,
		     "Invalid flag should be set if no templates are enrolled");
}

ZTEST_USER(fpsensor_match,
	   test_match_success_negative_finger_index_fails_internal)
{
	ec_response_fp_stats stats = {};

	load_example_template();

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = -1;
	finger_updated_bitmap = 0;
	mock_alg_match_fake.custom_fake = custom_match;

	trigger_match_attempt(FP_MODE_MATCH);

	expect_match_mkbp_event(EC_MKBP_FP_ERR_MATCH_NO_INTERNAL,
				FP_NO_SUCH_TEMPLATE & 0xF);

	zassert_ok(ec_cmd_fp_stats(nullptr, &stats));
	zassert_true(stats.timestamps_invalid & FPSTATS_MATCHING_INV);
}

ZTEST_USER(fpsensor_match,
	   test_match_success_out_of_bounds_finger_index_fails_internal)
{
	ec_response_fp_stats stats = {};

	load_example_template();

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = FP_MAX_FINGER_COUNT;
	finger_updated_bitmap = 0;
	mock_alg_match_fake.custom_fake = custom_match;

	trigger_match_attempt(FP_MODE_MATCH);

	expect_match_mkbp_event(EC_MKBP_FP_ERR_MATCH_NO_INTERNAL,
				FP_MAX_FINGER_COUNT);

	zassert_ok(ec_cmd_fp_stats(nullptr, &stats));
	zassert_true(stats.timestamps_invalid & FPSTATS_MATCHING_INV);
}

ZTEST_SUITE(fpsensor_match, nullptr, fpsensor_setup, fpsensor_before, nullptr,
	    nullptr);
