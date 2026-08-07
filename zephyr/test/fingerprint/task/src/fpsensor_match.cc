/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
#include <fpsensor/fpsensor_crypto.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>
#include <rollback.h>
#include <rollback_private.h>

#define ROLLBACK0_ADDR DT_REG_ADDR(DT_NODELABEL(rollback0))
#define ROLLBACK0_SIZE DT_REG_SIZE(DT_NODELABEL(rollback0))

#define ROLLBACK1_ADDR DT_REG_ADDR(DT_NODELABEL(rollback1))
#define ROLLBACK1_SIZE DT_REG_SIZE(DT_NODELABEL(rollback1))

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

#define fp_sim DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#define IMAGE_SIZE                                                 \
	MAX_FROM_LIST(LISTIFY(NUM_IMAGE_CAPTURE_TYPES,             \
			      FINGERPRINT_SENSOR_FRAME_SIZE, (, ), \
			      DT_CHOSEN(cros_fp_fingerprint_sensor)))
static uint8_t image_buffer[IMAGE_SIZE];

static const uint8_t fake_rollback_entropy[] = "some_rollback_entropy";

static const size_t test_info_buffer_size =
	sizeof(struct ec_response_fp_info_v3) +
	sizeof(struct fp_image_frame_params_v2) * FP_MAX_CAPTURE_TYPES;
static uint8_t buffer[test_info_buffer_size];
static struct ec_response_fp_info_v3 *test_info_buffer =
	(struct ec_response_fp_info_v3 *)buffer;

/* The fake TPM seed is "very_secret_32_bytes_of_tpm_seed" */
#define FAKE_TPM_SEED                                                       \
	{ 0x76, 0x65, 0x72, 0x79, 0x5f, 0x73, 0x65, 0x63, 0x72, 0x65, 0x74, \
	  0x5f, 0x33, 0x32, 0x5f, 0x62, 0x79, 0x74, 0x65, 0x73, 0x5f, 0x6f, \
	  0x66, 0x5f, 0x74, 0x70, 0x6d, 0x5f, 0x73, 0x65, 0x65, 0x64 }

/* The fake UserID is "i_m_a_fake_user_id_used_for_test" */
#define FAKE_USER_ID                                      \
	{ 0x5f6d5f69, 0x61665f61, 0x755f656b, 0x5f726573, \
	  0x755f6469, 0x5f646573, 0x5f726f66, 0x74736574 }

static const uint8_t example_template[CONFIG_FP_ALGORITHM_TEMPLATE_SIZE] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
	0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
	0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
};

static const uint8_t nonce[FP_CONTEXT_NONCE_BYTES] = { 0x94, 0x1e, 0xe3, 0x47,
						       0x31, 0x0b, 0x89, 0x73,
						       0x1a, 0xeb, 0xa4, 0x45 };

static const uint8_t salt[FP_CONTEXT_ENCRYPTION_SALT_BYTES] = {
	0x2b, 0x2e, 0x90, 0x58, 0xfa, 0x25, 0x3e, 0x3b,
	0x21, 0x35, 0x9a, 0x25, 0x79, 0x20, 0xba, 0x60
};

static const uint8_t pos_match_salt[FP_POSITIVE_MATCH_SALT_BYTES] = {
	0x0f, 0x8d, 0xe0, 0x47, 0x69, 0x0f, 0xda, 0xea,
	0xbc, 0xdc, 0x96, 0x7d, 0x69, 0x19, 0xac, 0xe7
};

/*
 * Buffer for encrypted template generated dynamically during test setup.
 */
static uint8_t example_template_encrypted[sizeof(struct ec_params_fp_template) +
					  FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE];

/*
 * Buffer for positive match secret generated dynamically.
 */
static uint8_t example_positive_match_secret[FP_POSITIVE_MATCH_SECRET_BYTES];

static int match_result;
static int32_t finger_index;
static uint32_t finger_updated_bitmap;

/**
 * @brief Dynamically encrypts example_template using current key derivation
 * rules.
 */
static void setup_example_template_encrypted(void)
{
	struct ec_params_fp_template *params =
		(struct ec_params_fp_template *)example_template_encrypted;
	struct ec_fp_template_encryption_metadata *meta =
		(struct ec_fp_template_encryption_metadata
			 *)(example_template_encrypted +
			    sizeof(struct ec_params_fp_template));
	uint8_t *payload = (uint8_t *)(meta + 1);

	params->offset = 0;
	params->size = FP_TEMPLATE_COMMIT |
		       FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE;

	meta->struct_version = FP_TEMPLATE_FORMAT_VERSION;

	memcpy(meta->nonce, nonce, sizeof(nonce));
	memcpy(meta->encryption_salt, salt, sizeof(salt));

	/* Copy template payload + positive match salt. */
	memcpy(payload, example_template, sizeof(example_template));
	memcpy(payload + sizeof(example_template), pos_match_salt,
	       sizeof(pos_match_salt));

	/* Derive the expected positive match secret for this salt. */
	zassert_equal(derive_positive_match_secret(
			      example_positive_match_secret, pos_match_salt,
			      global_context.user_id, global_context.tpm_seed),
		      EC_SUCCESS);

	/* Derive key and encrypt payload in-place. */
	FpEncryptionKey key;
	zassert_equal(derive_encryption_key(key, meta->encryption_salt,
					    global_context.user_id,
					    global_context.tpm_seed),
		      EC_SUCCESS);

	zassert_equal(aes_128_gcm_encrypt(
			      key,
			      std::span<const uint8_t>{
				      payload, sizeof(example_template) +
						       sizeof(pos_match_salt) },
			      std::span<uint8_t>{
				      payload, sizeof(example_template) +
						       sizeof(pos_match_salt) },
			      meta->nonce, meta->tag),
		      EC_SUCCESS);
}

/**
 * @brief Helper to set the FP mode and simulate a finger touch to trigger a
 * match.
 */
static void trigger_match_attempt(uint32_t mode)
{
	struct ec_params_fp_mode mode_params = { .mode = mode };
	struct ec_response_fp_mode mode_resp;
	struct fingerprint_sensor_state sensor_state;

	/* Switch mode to match and wait for task to switch. */
	zassert_ok(ec_cmd_fp_mode(NULL, &mode_params, &mode_resp));
	zassert_equal(mode_resp.mode & mode, mode,
		      "Failed to set all requested FP modes");

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &sensor_state);
	sensor_state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &sensor_state);

	/* Trigger the sensor callback and wait for processing. */
	fingerprint_run_callback(fp_sim);
	k_msleep(1);
}

ZTEST_USER(fpsensor_match, test_match_no_templates_mkbp_event)
{
	uint32_t fp_events;

	/* Trigger match with no templates loaded. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_MATCH
	 * - Match failed with NO_TEMPLATES
	 * - Finger ID is FP_NO_SUCH_TEMPLATE
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_MATCH);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events),
		      EC_MKBP_FP_ERR_MATCH_NO_TEMPLATES);
	zassert_equal(EC_MKBP_FP_MATCH_IDX(fp_events),
		      FP_NO_SUCH_TEMPLATE & 0xF);
}

ZTEST_USER(fpsensor_match, test_match_no_templates_mode_cleared)
{
	struct ec_params_fp_mode params = { 0 };
	struct ec_response_fp_mode response;

	/* Trigger match with no templates. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Confirm that capture mode is not enabled. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_MATCH);
}

static int match_compare(const struct fingerprint_algorithm *const alg,
			 void *templ, uint32_t templ_count,
			 const uint8_t *const image, bool template_update,
			 int32_t *match_index, uint32_t *update_bitmap)
{
	zassert_true(template_update);
	zassert_equal(templ_count, 1);
	zassert_mem_equal((uint8_t *)templ, example_template,
			  CONFIG_FP_ALGORITHM_TEMPLATE_SIZE);
	zassert_mem_equal(image, image_buffer, IMAGE_SIZE);

	return 0;
}

ZTEST_USER(fpsensor_match, test_match_correct_template_and_image)
{
	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	/*
	 * Use custom match function to check if template and scan passed to
	 * matching algorithm is correct.
	 */
	mock_alg_match_fake.custom_fake = match_compare;

	/* Prepare image. */
	memset(image_buffer, 1, IMAGE_SIZE);

	/* Load image to simulator. */
	fingerprint_load_image(fp_sim, image_buffer, IMAGE_SIZE);

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);
}

ZTEST_USER(fpsensor_match, test_match_no_match_mkbp_event)
{
	uint32_t fp_events;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	mock_alg_match_fake.return_val = FP_MATCH_RESULT_NO_MATCH;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_MATCH
	 * - Match failed with NO_MATCH
	 * - Finger ID is FP_NO_SUCH_TEMPLATE
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_MATCH);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events), EC_MKBP_FP_ERR_MATCH_NO);
	zassert_equal(EC_MKBP_FP_MATCH_IDX(fp_events),
		      FP_NO_SUCH_TEMPLATE & 0xF);
}

static int custom_match(const struct fingerprint_algorithm *const alg,
			void *templ, uint32_t templ_count,
			const uint8_t *const image, bool template_update,
			int32_t *match_index, uint32_t *update_bitmap)
{
	*match_index = finger_index;
	*update_bitmap = finger_updated_bitmap;

	return match_result;
}

static int match_no_update(const struct fingerprint_algorithm *const alg,
			   void *templ, uint32_t templ_count,
			   const uint8_t *const image, bool template_update,
			   int32_t *match_index, uint32_t *update_bitmap)
{
	zassert_false(template_update);
	zassert_equal(templ_count, 1);

	return 0;
}

ZTEST_USER(fpsensor_match, test_match_no_template_update)
{
	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	/*
	 * Use match_no_update function to check if FPSENSOR requested match
	 * without template update.
	 */
	mock_alg_match_fake.custom_fake = match_no_update;

	/* Trigger match without template update. */
	trigger_match_attempt(FP_MODE_MATCH | FP_MODE_MATCH_NO_TEMPLATE_UPDATE);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);
}

ZTEST_USER(fpsensor_match, test_match_success_mkbp_event)
{
	uint32_t fp_events;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_MATCH
	 * - Match succeeded with MATCH_YES
	 * - Finger ID is 0
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_MATCH);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events), EC_MKBP_FP_ERR_MATCH_YES);
	zassert_equal(EC_MKBP_FP_MATCH_IDX(fp_events), 0);
}

ZTEST_USER(fpsensor_match, test_match_success_template_updated_mkbp_event)
{
	uint32_t fp_events;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH_UPDATED;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_MATCH
	 * - Match succeeded with MATCH_YES_UPDATED
	 * - Finger ID is 0
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_MATCH);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events),
		      EC_MKBP_FP_ERR_MATCH_YES_UPDATED);
	zassert_equal(EC_MKBP_FP_MATCH_IDX(fp_events), 0);
}

ZTEST_USER(fpsensor_match, test_match_success_template_update_failed_mkbp_event)
{
	uint32_t fp_events;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH_UPDATE_FAILED;
	finger_index = 0;
	finger_updated_bitmap = 0;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_MATCH
	 * - Match succeeded with MATCH_YES_UPDATE_FAILED
	 * - Finger ID is 0
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_MATCH);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events),
		      EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED);
	zassert_equal(EC_MKBP_FP_MATCH_IDX(fp_events), 0);
}

ZTEST_USER(fpsensor_match, test_match_success_template_updated_dirty_template)
{
	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH_UPDATED;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that dirty templates bitmap is correct. */
	zassert_ok(ec_cmd_fp_info_v3(NULL, test_info_buffer,
				     test_info_buffer_size));
	zassert_equal(test_info_buffer->template_info.template_dirty, 0x1);
}

ZTEST_USER(fpsensor_match,
	   test_match_success_template_update_failed_dirty_template)
{
	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH_UPDATE_FAILED;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that dirty templates bitmap is correct. */
	zassert_ok(ec_cmd_fp_info_v3(NULL, test_info_buffer,
				     test_info_buffer_size));
	zassert_equal(test_info_buffer->template_info.template_dirty, 0x0);
}

ZTEST_USER(fpsensor_match, test_match_success_no_template_update_dirty_template)
{
	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that dirty templates bitmap is correct. */
	zassert_ok(ec_cmd_fp_info_v3(NULL, test_info_buffer,
				     test_info_buffer_size));
	zassert_equal(test_info_buffer->template_info.template_dirty, 0x0);
}

ZTEST_USER(fpsensor_match, test_match_no_template_update_dirty_template)
{
	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	/* Return MATCH_YES but NOT updated because we requested no update. */
	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Trigger match without update. */
	trigger_match_attempt(FP_MODE_MATCH | FP_MODE_MATCH_NO_TEMPLATE_UPDATE);

	/* Confirm that dirty templates bitmap is correct (no templates dirty).
	 */
	zassert_ok(ec_cmd_fp_info_v3(NULL, test_info_buffer,
				     test_info_buffer_size));
	zassert_equal(test_info_buffer->template_info.template_dirty, 0x0);
}

ZTEST_USER(fpsensor_match,
	   test_match_success_read_positive_match_secret_allowed)
{
	struct ec_params_fp_read_match_secret secret_params;
	struct ec_response_fp_read_match_secret secret_response;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

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
	zassert_ok(ec_cmd_fp_read_match_secret(NULL, &secret_params,
					       &secret_response));
	zassert_mem_equal(secret_response.positive_match_secret,
			  example_positive_match_secret,
			  FP_POSITIVE_MATCH_SECRET_BYTES);
}

ZTEST_USER(fpsensor_match,
	   test_match_success_read_positive_match_secret_timeout)
{
	struct ec_params_fp_read_match_secret secret_params;
	struct ec_response_fp_read_match_secret secret_response;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

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
		      ec_cmd_fp_read_match_secret(NULL, &secret_params,
						  &secret_response));
}

ZTEST_USER(fpsensor_match, test_match_success_read_positive_match_secret_twice)
{
	struct ec_params_fp_read_match_secret secret_params;
	struct ec_response_fp_read_match_secret secret_response;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

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
	zassert_ok(ec_cmd_fp_read_match_secret(NULL, &secret_params,
					       &secret_response));

	/*
	 * Confirm that we can't read positive match secret for the second time.
	 */
	zassert_equal(EC_RES_TIMEOUT,
		      ec_cmd_fp_read_match_secret(NULL, &secret_params,
						  &secret_response));
}

ZTEST_USER(fpsensor_match,
	   test_match_read_positive_match_secret_without_match_fails)
{
	struct ec_params_fp_read_match_secret secret_params;
	struct ec_response_fp_read_match_secret secret_response;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	mock_alg_match_fake.return_val = FP_MATCH_RESULT_NO_MATCH;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that we can't read positive match secret. */
	secret_params.fgr = 0;
	zassert_equal(EC_RES_TIMEOUT,
		      ec_cmd_fp_read_match_secret(NULL, &secret_params,
						  &secret_response));
}

ZTEST_USER(fpsensor_match, test_match_error_no_positive_match_secret)
{
	struct ec_params_fp_read_match_secret secret_params;
	struct ec_response_fp_read_match_secret secret_response;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	/* Negative value means error. */
	mock_alg_match_fake.return_val = -1;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that we can't read positive match secret. */
	secret_params.fgr = 0;
	zassert_equal(EC_RES_TIMEOUT,
		      ec_cmd_fp_read_match_secret(NULL, &secret_params,
						  &secret_response));
}

ZTEST_USER(fpsensor_match, test_match_error_mkbp_event)
{
	uint32_t fp_events;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	/* Negative value means error. */
	mock_alg_match_fake.return_val = -1;

	/* Trigger match. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_MATCH
	 * - Match failed with NO_INTERNAL
	 * - Finger ID is FP_NO_SUCH_TEMPLATE
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_MATCH);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events),
		      EC_MKBP_FP_ERR_MATCH_NO_INTERNAL);
	zassert_equal(EC_MKBP_FP_MATCH_IDX(fp_events),
		      FP_NO_SUCH_TEMPLATE & 0xF);
}

ZTEST_USER(fpsensor_match, test_match_no_match_keeps_stats_valid)
{
	struct ec_response_fp_stats stats = { 0 };

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	/* Mock a "No Match" result from the algorithm. */
	mock_alg_match_fake.return_val = FP_MATCH_RESULT_NO_MATCH;

	/* Trigger a match attempt. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Ensure the match algorithm was actually executed. */
	zassert_equal(mock_alg_match_fake.call_count, 1,
		      "Match algorithm was not called");

	/* Verify timestamps_invalid should not have FPSTATS_MATCHING_INV. */
	zassert_ok(ec_cmd_fp_stats(NULL, &stats));
	zassert_false(
		stats.timestamps_invalid & FPSTATS_MATCHING_INV,
		"Matching stats should be valid even on a 'No Match' result");
}

ZTEST_USER(fpsensor_match, test_match_internal_error_sets_invalid_flag)
{
	struct ec_response_fp_stats stats = { 0 };

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	/* Mock an internal error (Negative value means error). */
	mock_alg_match_fake.return_val = -1;

	/* Trigger a match attempt. */
	trigger_match_attempt(FP_MODE_MATCH);

	/* Ensure the match algorithm was actually executed. */
	zassert_equal(mock_alg_match_fake.call_count, 1,
		      "Match algorithm was not called");

	/* Verify timestamps_invalid should have FPSTATS_MATCHING_INV. */
	zassert_ok(ec_cmd_fp_stats(NULL, &stats));
	zassert_true(stats.timestamps_invalid & FPSTATS_MATCHING_INV,
		     "Internal error must set the invalid flag");
}

ZTEST_USER(fpsensor_match, test_match_no_templates_sets_invalid)
{
	struct ec_response_fp_stats stats = { 0 };

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
	zassert_ok(ec_cmd_fp_stats(NULL, &stats));
	zassert_true(stats.timestamps_invalid & FPSTATS_MATCHING_INV,
		     "Invalid flag should be set if no templates are enrolled");
}

static void *fpsensor_setup(void)
{
	struct ec_params_fp_seed fp_seed_params = {
		.struct_version = 4,
		.reserved = 0,
		.seed = FAKE_TPM_SEED,
	};
	const struct rollback_data data = {
		.id = 0,
		.rollback_min_version = 0,
#ifdef CONFIG_PLATFORM_EC_ROLLBACK_SECRET_SIZE
		.secret = { 0 },
#endif
		.cookie = CROS_EC_ROLLBACK_COOKIE,
	};

	zassert_ok(crec_flash_erase(ROLLBACK0_ADDR, ROLLBACK0_SIZE));
	zassert_ok(crec_flash_write(ROLLBACK0_ADDR, sizeof(data),
				    (const char *)&data));

	zassert_ok(crec_flash_erase(ROLLBACK1_ADDR, ROLLBACK1_SIZE));
	zassert_ok(crec_flash_write(ROLLBACK1_ADDR, sizeof(data),
				    (const char *)&data));

	/* Start shimmed tasks. */
	start_ec_tasks();
	k_msleep(100);

	/* Add some entropy to rollback region. */
	rollback_add_entropy(fake_rollback_entropy,
			     sizeof(fake_rollback_entropy) - 1);

	/* Set TPM seed. */
	ec_cmd_fp_seed(NULL, &fp_seed_params);

	return NULL;
}

static void fpsensor_before(void *f)
{
	struct fingerprint_sensor_state state = {
		.bad_pixels = 0,
		.maintenance_ran = false,
		.detect_mode = false,
		.low_power_mode = false,
		.finger_state = FINGERPRINT_FINGER_STATE_NONE,
		.init_result = 0,
		.deinit_result = 0,
		.config_result = 0,
		.get_info_result = 0,
		.acquire_image_result = FINGERPRINT_SENSOR_SCAN_GOOD,
		.last_acquire_image_mode = -1,
	};
	struct ec_params_fp_context_v1 fp_context_params = {
		.action = FP_CONTEXT_ASYNC,
		.userid = FAKE_USER_ID,
	};
	uint32_t fp_events;

	fingerprint_set_state(fp_sim, &state);
	RESET_FAKE(mkbp_send_event);

	RESET_FAKE(mock_alg_init);
	RESET_FAKE(mock_alg_exit);
	RESET_FAKE(mock_alg_enroll_start);
	RESET_FAKE(mock_alg_enroll_step);
	RESET_FAKE(mock_alg_enroll_finish);
	RESET_FAKE(mock_alg_match);

	match_result = 0;
	finger_index = 0;
	finger_updated_bitmap = 0;

	/* Set context (FP_CONTEXT_ASYNC). */
	ec_cmd_fp_context_v1(NULL, &fp_context_params);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Get context setting result. */
	fp_context_params.action = FP_CONTEXT_GET_RESULT;
	ec_cmd_fp_context_v1(NULL, &fp_context_params);

	/* Dynamically re-generate example_template_encrypted using current key
	 * derivation. */
	setup_example_template_encrypted();

	/* Clear MKBP events from previous tests. */
	fp_get_next_event((uint8_t *)&fp_events);
}

ZTEST_SUITE(fpsensor_match, NULL, fpsensor_setup, fpsensor_before, NULL, NULL);
