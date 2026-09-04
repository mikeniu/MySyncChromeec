/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "crypto/elliptic_curve_key.h"
#include "ec_commands.h"
#include "ec_tasks.h"
#include "flash.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_state.h"
#include "mock/fpsensor_state_mock.h"
#include "mock/otpi_mock.h"
#include "openssl/aes.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/evp.h"
#include "openssl/obj_mac.h"
#include "rollback.h"
#include "rollback_private.h"
#include "sha256.h"

#define ROLLBACK0_ADDR DT_REG_ADDR(DT_NODELABEL(rollback0))
#define ROLLBACK0_SIZE DT_REG_SIZE(DT_NODELABEL(rollback0))

#define ROLLBACK1_ADDR DT_REG_ADDR(DT_NODELABEL(rollback1))
#define ROLLBACK1_SIZE DT_REG_SIZE(DT_NODELABEL(rollback1))
#include "util.h"

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <span>
#include <variant>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

/* Function used to cleanup session secrets and flags. */
void reset_session(void);

/* Challenge creation time. */
extern timestamp_t challenge_ctime;

extern "C" enum ec_status test_send_host_command(int command, int version,
						 const void *params,
						 int params_size, void *resp,
						 int resp_size)
{
	struct host_cmd_handler_args args;

	args.command = command;
	args.version = version;
	args.params = params;
	args.params_size = params_size;
	args.response = resp;
	args.response_max = resp_size;
	args.response_size = 0;

	return (enum ec_status)host_command_process(&args);
}

namespace
{

/* Shared constants */
constexpr uint8_t kTemplateFillByte = 0xc4;
constexpr uint8_t kTestSaltTrivial = 0x00;
constexpr uint8_t kTestSaltNonTrivial = 0xab;

constexpr fp_elliptic_curve_public_key kTestPeerPubKey = {
	.x = {
		0x85, 0xAD, 0x35, 0x23, 0x05, 0x1E, 0x33, 0x3F,
		0xCA, 0xA7, 0xEA, 0xA5, 0x88, 0x33, 0x12, 0x95,
		0xA7, 0xB5, 0x98, 0x9F, 0x32, 0xEF, 0x7D, 0xE9,
		0xF8, 0x70, 0x14, 0x5E, 0x89, 0xCB, 0xDE, 0x1F,
	},
	.y = {
		0xD1, 0xDC, 0x91, 0xC6, 0xE6, 0x5B, 0x1E, 0x3C,
		0x01, 0x6C, 0xE6, 0x50, 0x25, 0x5D, 0x89, 0xCF,
		0xB7, 0x8D, 0x88, 0xB9, 0x0D, 0x09, 0x41, 0xF1,
		0x09, 0x4F, 0x61, 0x55, 0x6C, 0xC4, 0x96, 0x6B,
	},
};

constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> kDefaultTpmSeed = {
	1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
	6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
};

constexpr std::array<uint8_t, 4> kTestUserId = { 0x0a, 0x00, 0x00, 0x00 };
constexpr std::array<uint8_t, 8> kDefaultContextUserId = { 10, 0, 0, 0,
							   0,  0, 0, 0 };
constexpr std::array<uint8_t, 12> kSenderFingerGuard = { 'f', 'i', 'n', 'g',
							 'e', 'r', '_', 'g',
							 'u', 'a', 'r', 'd' };
constexpr std::array<uint8_t, 5> kSenderFpmcu = { 'f', 'p', 'm', 'c', 'u' };
constexpr std::array<uint8_t, 6> kOpEnroll = { 'e', 'n', 'r', 'o', 'l', 'l' };
constexpr std::array<uint8_t, 4> kOpAuth = { 'a', 'u', 't', 'h' };
constexpr std::array<uint8_t, 13> kOpEnrollFinish = { 'e', 'n', 'r', 'o', 'l',
						      'l', '_', 'f', 'i', 'n',
						      'i', 's', 'h' };

static void
set_test_fp_context(std::array<uint8_t, 8> userid = kDefaultContextUserId)
{
	struct ec_params_fp_context_v1 ctx_params = {
		.action = FP_CONTEXT_GET_RESULT,
	};
	std::ranges::copy(userid, ctx_params.userid);
	zassert_equal(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &ctx_params,
					     sizeof(ctx_params), nullptr, 0),
		      EC_RES_SUCCESS);
}

/*
 * Packed wire format of the EC_CMD_FP_TEMPLATE v0 payload (metadata, template
 * ciphertext, and salt).
 */
struct __packed test_v0_template_layout {
	uint32_t offset;
	uint32_t size;
	struct ec_fp_template_encryption_metadata metadata;
	uint8_t template_data[sizeof(fp_template[0])];
	uint8_t salt[sizeof(global_context.fp_positive_match_salt[0])];
};

static void
setup_and_encrypt_v0_template(struct test_v0_template_layout &params,
			      uint8_t salt_fill, bool corrupt_tag)
{
	struct ec_params_fp_seed seed_params = {
		.struct_version = FP_TEMPLATE_FORMAT_VERSION,
		.reserved = 0,
		.seed = { 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
			  6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1 },
	};
	zassert_equal(test_send_host_command(EC_CMD_FP_SEED, 0, &seed_params,
					     sizeof(seed_params), NULL, 0),
		      EC_RES_SUCCESS);

	set_test_fp_context({ 0, 1, 2, 3, 4, 5, 6, 7 });

	params.offset = 0;
	params.size = static_cast<uint32_t>(
		FP_TEMPLATE_COMMIT |
		(sizeof(params) -
		 offsetof(struct test_v0_template_layout, metadata)));

	params.metadata.struct_version = FP_TEMPLATE_FORMAT_VERSION;

	std::ranges::fill(params.template_data, kTemplateFillByte);
	std::ranges::fill(params.salt, salt_fill);

	struct fp_auth_command_encryption_metadata info;
	uint8_t *payload_start = params.template_data;
	size_t payload_size =
		sizeof(params.template_data) + sizeof(params.salt);

	encrypt_data_in_place(1, info, global_context.user_id,
			      global_context.tpm_seed,
			      { payload_start, payload_size });

	std::ranges::copy(info.nonce, params.metadata.nonce);
	std::ranges::copy(info.encryption_salt,
			  params.metadata.encryption_salt);
	std::ranges::copy(info.tag, params.metadata.tag);

	if (corrupt_tag) {
		params.metadata.tag[0] ^= 0x01;
	}
}

static enum ec_error_list get_fp_encryption_status(uint32_t *status)
{
	struct ec_response_fp_encryption_status resp = { 0 };

	zassert_equal(test_send_host_command(EC_CMD_FP_ENC_STATUS, 0, NULL, 0,
					     &resp, sizeof(resp)),
		      EC_RES_SUCCESS);
	*status = resp.status;

	return EC_SUCCESS;
}

static enum ec_status wait_for_template_decrypt_result(void)
{
	struct ec_params_fp_template_v1 result_params = {
		.cmd = FP_TEMPLATE_GET_RESULT,
	};
	enum ec_status res;
	int timeout = 50; /* Poll up to 500ms */

	do {
		k_msleep(10);
		res = test_send_host_command(EC_CMD_FP_TEMPLATE, 1,
					     &result_params,
					     sizeof(result_params), NULL, 0);
	} while (res == EC_RES_BUSY && --timeout > 0);

	return res;
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_pairing_key_keygen)
{
	enum ec_status rv;
	struct ec_response_fp_establish_pairing_key_keygen keygen_response;

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	bssl::UniquePtr<EC_KEY> pubkey =
		create_ec_key_from_pubkey(keygen_response.pubkey);

	zassert_not_equal(pubkey.get(), nullptr);
	zassert_equal(EC_KEY_check_key(pubkey.get()), 1);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_and_load_pairing_key)
{
	enum ec_status rv;
	ec_response_fp_establish_pairing_key_keygen keygen_response;
	ec_params_fp_establish_pairing_key_wrap wrap_params{
		.peers_pubkey = kTestPeerPubKey,
	};
	ec_response_fp_establish_pairing_key_wrap wrap_response;
	ec_params_fp_load_pairing_key load_params;

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	memcpy(&load_params.encrypted_pairing_key.info,
	       &wrap_response.encrypted_pairing_key.info,
	       sizeof(wrap_response.encrypted_pairing_key.info));

	memcpy(load_params.encrypted_pairing_key.data,
	       wrap_response.encrypted_pairing_key.data,
	       sizeof(wrap_response.encrypted_pairing_key.data));

	rv = test_send_host_command(EC_CMD_FP_LOAD_PAIRING_KEY, 0, &load_params,
				    sizeof(load_params), NULL, 0);

	zassert_equal(rv, EC_RES_SUCCESS);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_pairing_key_fail)
{
	enum ec_status rv;
	struct ec_params_fp_establish_pairing_key_wrap wrap_params{
		.peers_pubkey = kTestPeerPubKey,
	};
	struct ec_response_fp_establish_pairing_key_wrap wrap_response;

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));

	zassert_not_equal(rv, EC_RES_SUCCESS);
}

ZTEST(fpsensor_auth_commands, test_fp_command_load_pairing_key_invalid)
{
	enum ec_status rv;
	ec_response_fp_establish_pairing_key_keygen keygen_response;
	ec_params_fp_establish_pairing_key_wrap wrap_params{
		.peers_pubkey = kTestPeerPubKey,
	};
	ec_response_fp_establish_pairing_key_wrap wrap_response;
	ec_params_fp_load_pairing_key load_params;

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	/* No encryption info. */
	memset(&load_params.encrypted_pairing_key.info, 0,
	       sizeof(load_params.encrypted_pairing_key.info));

	memcpy(load_params.encrypted_pairing_key.data,
	       wrap_response.encrypted_pairing_key.data,
	       sizeof(wrap_response.encrypted_pairing_key.data));

	rv = test_send_host_command(EC_CMD_FP_LOAD_PAIRING_KEY, 0, &load_params,
				    sizeof(load_params), NULL, 0);

	zassert_equal(rv, EC_RES_UNAVAILABLE);
}

ZTEST(fpsensor_auth_commands, test_fp_command_generate_nonce)
{
	enum ec_status rv;
	struct ec_response_fp_generate_nonce nonce_response;

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	zassert_equal(rv, EC_RES_SUCCESS);
}

static enum ec_error_list
initialize_pairing_key(std::span<uint8_t, FP_PAIRING_KEY_LEN> pairing_key)
{
	enum ec_status rv;
	struct ec_response_fp_establish_pairing_key_keygen keygen_response;
	struct ec_params_fp_establish_pairing_key_wrap wrap_params;
	ec_response_fp_establish_pairing_key_wrap wrap_response;
	ec_params_fp_load_pairing_key load_params;

	/* Ask FPMCU for its public key */
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));
	zassert_equal(rv, EC_RES_SUCCESS);

	/* Convert FPMCU public key to more useful form */
	bssl::UniquePtr<EC_KEY> fpmcu_public_key =
		create_ec_key_from_pubkey(keygen_response.pubkey);
	zassert_not_equal(fpmcu_public_key.get(), nullptr);

	/* Generate ECDH private key on our side */
	bssl::UniquePtr<EC_KEY> ecdh_key = generate_elliptic_curve_key();
	zassert_not_equal(ecdh_key.get(), nullptr);

	/* Get public key from that key */
	std::optional<fp_elliptic_curve_public_key> pubkey =
		create_pubkey_from_ec_key(*ecdh_key);
	zassert_true(pubkey.has_value());

	wrap_params.peers_pubkey = pubkey.value();

	/* Generate Pairing Key on our side */
	enum ec_error_list ret = generate_ecdh_shared_secret_without_kdf(
		*ecdh_key, *fpmcu_public_key, pairing_key);
	zassert_equal(ret, EC_SUCCESS);

	/*
	 * Send our public key to the FPMCU. FPMCU will return encrypted
	 * Pairing Key.
	 */
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));
	zassert_equal(rv, EC_RES_SUCCESS);

	load_params.encrypted_pairing_key = wrap_response.encrypted_pairing_key;

	/* Load Pairing Key to the FPMCU */
	rv = test_send_host_command(EC_CMD_FP_LOAD_PAIRING_KEY, 0, &load_params,
				    sizeof(load_params), nullptr, 0);
	zassert_equal(rv, EC_RES_SUCCESS);

	return EC_SUCCESS;
}

static enum ec_error_list generate_valid_establish_session_request(
	std::span<const uint8_t, FP_PAIRING_KEY_LEN> pairing_key,
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed,
	struct ec_params_fp_establish_session *session_params)
{
	static constexpr uint8_t tpm_seed_aad[] = { 't', 'p', 'm', '_',
						    's', 'e', 'e', 'd' };
	constexpr auto aad = std::span{ tpm_seed_aad };

	/* Get our session nonce */
	constexpr std::array<uint8_t, FP_CK_SESSION_NONCE_LEN> session_nonce = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	/* Obtain session key on our side */
	std::array<uint8_t, SHA256_DIGEST_SIZE> session_key;
	enum ec_error_list ret = generate_session_key(
		fpmcu_nonce, session_nonce, pairing_key, {}, session_key);
	zassert_equal(ret, EC_SUCCESS);

	zassert_equal(tpm_seed.size(), sizeof(session_params->enc_tpm_seed));

	constexpr std::array<uint8_t, FP_AES_KEY_NONCE_BYTES> nonce = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};
	zassert_equal(nonce.size(), sizeof(session_params->nonce));
	memcpy(session_params->nonce, nonce.data(), FP_AES_KEY_NONCE_BYTES);

	/* Encrypt tpm_seed using session key */
	bssl::ScopedEVP_AEAD_CTX ctx;
	int aead_ret = EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_256_gcm(),
					 session_key.data(), session_key.size(),
					 sizeof(session_params->tag), nullptr);
	zassert_equal(aead_ret, 1);

	size_t tag_bytes_written = 0;
	aead_ret = EVP_AEAD_CTX_seal_scatter(
		ctx.get(), session_params->enc_tpm_seed, session_params->tag,
		&tag_bytes_written, sizeof(session_params->tag),
		session_params->nonce, sizeof(session_params->nonce),
		tpm_seed.data(), tpm_seed.size(), /* extra_in = */ nullptr,
		/* extra_in_len = */ 0, aad.data(), aad.size());
	zassert_equal(aead_ret, 1);
	zassert_equal(tag_bytes_written, sizeof(session_params->tag));

	/* Copy our session nonce to the structure */
	std::ranges::copy(session_nonce, session_params->peer_nonce);

	return EC_SUCCESS;
}

static enum ec_error_list
establish_session(std::span<uint8_t, SHA256_DIGEST_LENGTH> session_key,
		  std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed =
			  kDefaultTpmSeed)
{
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key{};
	zassert_equal(initialize_pairing_key(pairing_key), EC_SUCCESS);

	struct ec_response_fp_generate_nonce nonce_response{};
	struct ec_params_fp_establish_session session_params{};

	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0,
					     nullptr, 0, &nonce_response,
					     sizeof(nonce_response)),
		      EC_RES_SUCCESS);

	constexpr std::array<uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	zassert_equal(generate_session_key(nonce_response.nonce, peer_nonce,
					   pairing_key, {}, session_key),
		      EC_SUCCESS);

	zassert_equal(generate_valid_establish_session_request(
			      pairing_key, nonce_response.nonce, tpm_seed,
			      &session_params),
		      EC_SUCCESS);

	zassert_equal(test_send_host_command(
			      EC_CMD_FP_ESTABLISH_SESSION, 0, &session_params,
			      sizeof(session_params), nullptr, 0),
		      EC_RES_SUCCESS);

	return EC_SUCCESS;
}

static enum ec_error_list establish_session(void)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key;
	return establish_session(session_key);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	uint32_t status;

	zassert_equal(initialize_pairing_key(pairing_key), EC_SUCCESS);

	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);
	zassert_true((((int)status) & (FP_ENC_STATUS_SEED_SET)) == 0);

	global_context.templ_valid = 1;

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);
	zassert_true((((int)status) & (FP_ENC_STATUS_SEED_SET)) == 0);

	zassert_equal(generate_valid_establish_session_request(
			      pairing_key, nonce_response.nonce,
			      kDefaultTpmSeed, &session_params),
		      EC_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	zassert_equal(rv, EC_RES_SUCCESS);

	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);
	zassert_true((((int)status) & (FP_ENC_STATUS_SEED_SET)) ==
		     (FP_ENC_STATUS_SEED_SET));

	zassert_equal(global_context.templ_valid, 0u);
}

ZTEST(fpsensor_auth_commands,
      test_fp_command_establish_session_fail_different_pk)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;

	zassert_equal(initialize_pairing_key(pairing_key), EC_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	zassert_equal(generate_valid_establish_session_request(
			      pairing_key, nonce_response.nonce,
			      kDefaultTpmSeed, &session_params),
		      EC_SUCCESS);

	// Change Pairing Key to different one.
	zassert_equal(initialize_pairing_key(pairing_key), EC_SUCCESS);

	// Try to establish session using request prepared for previous
	// Pairing Key.
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	// Expect failure in TPM Seed decryption.
	zassert_equal(rv, EC_RES_ERROR);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_deny)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;

	zassert_equal(initialize_pairing_key(pairing_key), EC_SUCCESS);

	// Establish session without generate nonce should fail.
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	zassert_equal(rv, EC_RES_ACCESS_DENIED);

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	zassert_equal(generate_valid_establish_session_request(
			      pairing_key, nonce_response.nonce,
			      kDefaultTpmSeed, &session_params),
		      EC_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	zassert_equal(rv, EC_RES_SUCCESS);
}

ZTEST(fpsensor_auth_commands,
      test_fp_command_establish_session_limit_without_generated_nonce)
{
	enum ec_status rv;
	struct ec_params_fp_establish_session session_params;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;

	zassert_equal(initialize_pairing_key(pairing_key), EC_SUCCESS);

	/* Call nonce context without generated nonce should fail. */
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	zassert_equal(rv, EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands,
      test_fp_command_establish_session_limit_normal_context)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;

	zassert_equal(initialize_pairing_key(pairing_key), EC_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	/* Normal context should not clear the generated nonce. */
	/* This will be used for the migration path. */
	set_test_fp_context();

	zassert_equal(generate_valid_establish_session_request(
			      pairing_key, nonce_response.nonce,
			      kDefaultTpmSeed, &session_params),
		      EC_SUCCESS);

	/* Call nonce context with generated nonce should success. */
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	zassert_equal(rv, EC_RES_SUCCESS);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_limit_twice_1)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;

	zassert_equal(initialize_pairing_key(pairing_key), EC_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	zassert_equal(generate_valid_establish_session_request(
			      pairing_key, nonce_response.nonce,
			      kDefaultTpmSeed, &session_params),
		      EC_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	zassert_equal(rv, EC_RES_SUCCESS);

	/* Call nonce context twice should fail. */
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	zassert_equal(rv, EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_limit_twice_2)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;

	zassert_equal(initialize_pairing_key(pairing_key), EC_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	zassert_equal(generate_valid_establish_session_request(
			      pairing_key, nonce_response.nonce,
			      kDefaultTpmSeed, &session_params),
		      EC_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	zassert_equal(rv, EC_RES_SUCCESS);

	/*
	 * Call nonce context twice should fail even if two nonces were
	 * generated.
	 */
	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	zassert_equal(rv, EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_load_pk)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	struct ec_response_fp_establish_pairing_key_keygen keygen_response;
	struct ec_params_fp_establish_pairing_key_wrap wrap_params{
		.peers_pubkey = kTestPeerPubKey,
	};
	ec_response_fp_establish_pairing_key_wrap wrap_response;
	ec_params_fp_load_pairing_key load_params;

	zassert_equal(initialize_pairing_key(pairing_key), EC_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	memcpy(&load_params.encrypted_pairing_key.info,
	       &wrap_response.encrypted_pairing_key.info,
	       sizeof(wrap_response.encrypted_pairing_key.info));

	memcpy(load_params.encrypted_pairing_key.data,
	       wrap_response.encrypted_pairing_key.data,
	       sizeof(wrap_response.encrypted_pairing_key.data));

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	zassert_equal(rv, EC_RES_SUCCESS);

	zassert_equal(generate_valid_establish_session_request(
			      pairing_key, nonce_response.nonce,
			      kDefaultTpmSeed, &session_params),
		      EC_SUCCESS);

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	zassert_equal(rv, EC_RES_SUCCESS);

	/* Pairing key can be loaded after the session was established. */
	rv = test_send_host_command(EC_CMD_FP_LOAD_PAIRING_KEY, 0, &load_params,
				    sizeof(load_params), NULL, 0);

	zassert_equal(rv, EC_RES_SUCCESS);
}

struct v1_template_options {
	uint16_t struct_version = 4;
	bool include_salt = true;
	uint8_t salt_fill = 0xab;
	bool encrypt = true;
};

/*
 * Packed wire format of the EC_CMD_FP_TEMPLATE v1 load request payload.
 */
struct __packed test_v1_template_layout {
	uint32_t offset;
	uint32_t size;
	uint8_t cmd;
	struct ec_fp_template_encryption_metadata metadata;
	uint8_t template_data[sizeof(fp_template[0])];
	uint8_t salt[sizeof(global_context.fp_positive_match_salt[0])];
};

static_assert(offsetof(test_v1_template_layout, metadata) ==
		      offsetof(ec_params_fp_template_v1, data),
	      "v1 template layout metadata offset mismatch");

static void load_v1_template(const v1_template_options &opts = {})
{
	test_v1_template_layout params{};

	const size_t payload_size =
		sizeof(params.template_data) +
		(opts.include_salt ? sizeof(params.salt) : 0);
	const size_t total_size =
		offsetof(test_v1_template_layout, template_data) + payload_size;

	params.offset = 0;
	params.size = static_cast<uint32_t>(
		total_size - offsetof(test_v1_template_layout, metadata));
	params.cmd = FP_TEMPLATE_LOAD;

	params.metadata.struct_version = opts.struct_version;

	std::ranges::fill(params.template_data, kTemplateFillByte);
	if (opts.include_salt) {
		std::ranges::fill(params.salt, opts.salt_fill);
	}

	if (opts.encrypt) {
		struct fp_auth_command_encryption_metadata info;
		encrypt_data_in_place(1, info, global_context.user_id,
				      global_context.tpm_seed,
				      { params.template_data, payload_size });

		std::ranges::copy(info.nonce, params.metadata.nonce);
		std::ranges::copy(info.encryption_salt,
				  params.metadata.encryption_salt);
		std::ranges::copy(info.tag, params.metadata.tag);
	}

	zassert_equal(test_send_host_command(EC_CMD_FP_TEMPLATE, 1, &params,
					     total_size, nullptr, 0),
		      EC_RES_SUCCESS);
}

ZTEST(fpsensor_auth_commands, test_fp_command_template_v1_decrypted)
{
	struct ec_response_fp_generate_nonce nonce_response;
	uint32_t status;

	zassert_equal(establish_session(), EC_SUCCESS);

	load_v1_template();

	struct ec_params_fp_template_v1 decrypt_params = {
		.cmd = FP_TEMPLATE_DECRYPT,
	};
	zassert_equal(test_send_host_command(EC_CMD_FP_TEMPLATE, 1,
					     &decrypt_params,
					     sizeof(decrypt_params), NULL, 0),
		      EC_RES_SUCCESS);

	zassert_equal(wait_for_template_decrypt_result(), EC_RES_SUCCESS);

	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);

	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL,
					     0, &nonce_response,
					     sizeof(nonce_response)),
		      EC_RES_SUCCESS);

	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);
}

/* Test that legacy format (v3) isn't accepted by commit function. */
ZTEST(fpsensor_auth_commands, test_fp_command_template_v1_commit_v3)
{
	zassert_equal(establish_session(), EC_SUCCESS);

	load_v1_template({ .struct_version = 3, .include_salt = false });

	struct ec_params_fp_template_v1 decrypt_params = {
		.cmd = FP_TEMPLATE_DECRYPT,
	};

	zassert_equal(test_send_host_command(EC_CMD_FP_TEMPLATE, 1,
					     &decrypt_params,
					     sizeof(decrypt_params), NULL, 0),
		      EC_RES_SUCCESS);

	zassert_equal(wait_for_template_decrypt_result(), EC_RES_INVALID_PARAM);
}

/* Test that trivial positive match salt will be detected into an error. */
ZTEST(fpsensor_auth_commands, test_fp_command_template_v1_commit_trivial_salt)
{
	zassert_equal(establish_session(), EC_SUCCESS);
	set_test_fp_context({ 0, 1, 2, 3, 4, 5, 6, 7 });

	load_v1_template({ .salt_fill = 0x00 });

	struct ec_params_fp_template_v1 decrypt_params = {
		.cmd = FP_TEMPLATE_DECRYPT,
	};
	zassert_equal(test_send_host_command(EC_CMD_FP_TEMPLATE, 1,
					     &decrypt_params,
					     sizeof(decrypt_params), NULL, 0),
		      EC_RES_SUCCESS);

	zassert_equal(wait_for_template_decrypt_result(), EC_RES_INVALID_PARAM);
}

ZTEST(fpsensor_auth_commands, test_fp_command_template_v1_commit_without_seed)
{
	set_test_fp_context({ 0, 1, 2, 3, 4, 5, 6, 7 });

	load_v1_template({ .salt_fill = 0x12, .encrypt = false });

	struct ec_params_fp_template_v1 decrypt_params = {
		.cmd = FP_TEMPLATE_DECRYPT,
	};
	zassert_equal(test_send_host_command(EC_CMD_FP_TEMPLATE, 1,
					     &decrypt_params,
					     sizeof(decrypt_params), NULL, 0),
		      EC_RES_SUCCESS);

	zassert_equal(wait_for_template_decrypt_result(), EC_RES_UNAVAILABLE);
}

static void generate_and_sign_challenge(
	std::span<const uint8_t, SHA256_DIGEST_LENGTH> session_key,
	std::span<const uint8_t> user_id, std::span<const uint8_t> sender,
	std::span<const uint8_t> operation,
	std::span<uint8_t, SHA256_DIGEST_LENGTH> mac_out)
{
	struct ec_response_fp_generate_challenge challenge_response{};
	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					     nullptr, 0, &challenge_response,
					     sizeof(challenge_response)),
		      EC_RES_SUCCESS);

	zassert_equal(compute_message_signature(
			      session_key, user_id, sender, operation,
			      challenge_response.challenge, mac_out),
		      EC_SUCCESS);
}

ZTEST(fpsensor_auth_commands, test_fp_command_generate_challenge)
{
	struct ec_response_fp_generate_challenge challenge_response{};
	uint32_t status;

	zassert_equal(establish_session(), EC_SUCCESS);

	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);
	zassert_true((((int)status) & (FP_AUTH_CHALLENGE_SET)) == 0);

	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					     nullptr, 0, &challenge_response,
					     sizeof(challenge_response)),
		      EC_RES_SUCCESS);

	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);
	zassert_true((((int)status) & (FP_AUTH_CHALLENGE_SET)) ==
		     (FP_AUTH_CHALLENGE_SET));
}

ZTEST(fpsensor_auth_commands,
      test_fp_command_generate_challenge_fail_no_session)
{
	struct ec_response_fp_generate_challenge challenge_response{};

	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					     nullptr, 0, &challenge_response,
					     sizeof(challenge_response)),
		      EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_validate_request)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac{};
	uint32_t status;

	zassert_equal(establish_session(session_key), EC_SUCCESS);

	generate_and_sign_challenge(session_key, kTestUserId,
				    kSenderFingerGuard, kOpEnroll, mac);

	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);
	zassert_true((((int)status) & (FP_AUTH_CHALLENGE_SET)) ==
		     (FP_AUTH_CHALLENGE_SET));

	zassert_equal(validate_request(kTestUserId, kOpEnroll, mac),
		      EC_SUCCESS);

	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);
	zassert_true((((int)status) & (FP_AUTH_CHALLENGE_SET)) == 0);
}

ZTEST(fpsensor_auth_commands, test_fp_validate_request_fail_no_challenge)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac = { 0 };

	zassert_equal(establish_session(), EC_SUCCESS);

	zassert_equal(validate_request(kTestUserId, kOpEnroll, mac),
		      EC_ERROR_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_validate_request_fail_invalid_signature)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac = { 0 };
	struct ec_response_fp_generate_challenge challenge_response{};

	zassert_equal(establish_session(), EC_SUCCESS);

	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					     nullptr, 0, &challenge_response,
					     sizeof(challenge_response)),
		      EC_RES_SUCCESS);

	zassert_equal(validate_request(kTestUserId, kOpEnroll, mac),
		      EC_ERROR_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_validate_request_fail_timeout)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac{};
	struct ec_response_fp_generate_challenge challenge_response{};

	zassert_equal(establish_session(session_key), EC_SUCCESS);

	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					     nullptr, 0, &challenge_response,
					     sizeof(challenge_response)),
		      EC_RES_SUCCESS);

	if (challenge_ctime.val >= 6 * USEC_PER_SEC) {
		challenge_ctime.val -= 6 * USEC_PER_SEC;
	} else {
		k_sleep(K_SECONDS(6));
	}

	zassert_equal(compute_message_signature(
			      session_key, kTestUserId, kSenderFingerGuard,
			      kOpEnroll, challenge_response.challenge, mac),
		      EC_SUCCESS);

	zassert_equal(validate_request(kTestUserId, kOpEnroll, mac),
		      EC_ERROR_TIMEOUT);
}

ZTEST(fpsensor_auth_commands, test_fp_sign_message)
{
	constexpr std::array<const uint8_t, FP_CHALLENGE_SIZE> challenge = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5,
	};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> expected_mac{};

	zassert_equal(establish_session(session_key), EC_SUCCESS);

	zassert_equal(sign_message(kTestUserId, kOpEnroll, challenge, mac),
		      EC_SUCCESS);

	zassert_equal(compute_message_signature(session_key, kTestUserId,
						kSenderFpmcu, kOpEnroll,
						challenge, expected_mac),
		      EC_SUCCESS);

	zassert_mem_equal(mac.data(), expected_mac.data(), mac.size());
}

ZTEST(fpsensor_auth_commands, test_fp_sign_message_fail_no_session)
{
	constexpr std::array<const uint8_t, FP_CHALLENGE_SIZE> challenge = { 0 };
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac{};

	zassert_equal(sign_message(kTestUserId, kOpEnroll, challenge, mac),
		      EC_ERROR_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_mode_match_correct_signature)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_params_fp_mode_v1 params = { .mode = FP_MODE_MATCH };
	struct ec_response_fp_mode response{};

	zassert_equal(establish_session(session_key), EC_SUCCESS);
	set_test_fp_context();

	generate_and_sign_challenge(session_key, kTestUserId,
				    kSenderFingerGuard, kOpAuth, params.mac);

	zassert_equal(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode, FP_MODE_MATCH);
}

ZTEST(fpsensor_auth_commands, test_fp_mode_disable_match_no_signature)
{
	struct ec_params_fp_mode_v1 params = { .mode = 0 };
	struct ec_response_fp_mode response{};

	zassert_equal(establish_session(), EC_SUCCESS);

	global_context.sensor_mode = FP_MODE_MATCH;

	zassert_equal(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode, 0u, "%x");
}

ZTEST(fpsensor_auth_commands, test_fp_mode_match_invalid_signature)
{
	struct ec_response_fp_generate_challenge challenge_response{};
	struct ec_params_fp_mode_v1 params = { .mode = FP_MODE_MATCH };
	struct ec_response_fp_mode response{};

	zassert_equal(establish_session(), EC_SUCCESS);
	set_test_fp_context();

	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					     nullptr, 0, &challenge_response,
					     sizeof(challenge_response)),
		      EC_RES_SUCCESS);

	zassert_equal(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_mode_enroll_correct_signature)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_params_fp_mode_v1 params = { .mode = FP_MODE_ENROLL_SESSION |
						       FP_MODE_ENROLL_IMAGE };
	struct ec_response_fp_mode response{};

	zassert_equal(establish_session(session_key), EC_SUCCESS);
	set_test_fp_context();

	generate_and_sign_challenge(session_key, kTestUserId,
				    kSenderFingerGuard, kOpEnroll, params.mac);

	zassert_equal(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode,
		      FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE);
}

ZTEST(fpsensor_auth_commands, test_fp_mode_disable_enroll_no_signature)
{
	struct ec_params_fp_mode_v1 params = { .mode = 0 };
	struct ec_response_fp_mode response{};

	zassert_equal(establish_session(), EC_SUCCESS);

	global_context.sensor_mode = FP_MODE_ENROLL_SESSION;

	zassert_equal(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode, 0u, "%x");
}

ZTEST(fpsensor_auth_commands, test_fp_mode_enroll_invalid_signature)
{
	struct ec_response_fp_generate_challenge challenge_response{};
	struct ec_params_fp_mode_v1 params = { .mode = FP_MODE_ENROLL_SESSION |
						       FP_MODE_ENROLL_IMAGE };
	struct ec_response_fp_mode response{};

	zassert_equal(establish_session(), EC_SUCCESS);
	set_test_fp_context();

	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					     nullptr, 0, &challenge_response,
					     sizeof(challenge_response)),
		      EC_RES_SUCCESS);

	zassert_equal(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_mode_enroll_session_transitions)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_params_fp_mode_v1 params = { .mode = FP_MODE_ENROLL_SESSION |
						       FP_MODE_ENROLL_IMAGE };
	struct ec_response_fp_mode response{};

	zassert_equal(establish_session(session_key), EC_SUCCESS);
	set_test_fp_context();

	generate_and_sign_challenge(session_key, kTestUserId,
				    kSenderFingerGuard, kOpEnroll, params.mac);

	zassert_equal(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode,
		      FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE);

	params.mode = FP_MODE_ENROLL_SESSION | FP_MODE_FINGER_UP;
	memset(params.mac, 0, sizeof(params.mac));
	global_context.sensor_mode = FP_MODE_ENROLL_SESSION;

	zassert_equal(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode,
		      FP_MODE_ENROLL_SESSION | FP_MODE_FINGER_UP);

	params.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE;
	global_context.sensor_mode = FP_MODE_ENROLL_SESSION;

	zassert_equal(test_send_host_command(EC_CMD_FP_MODE, 1, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode,
		      FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE);
}

ZTEST(fpsensor_auth_commands, test_fp_mode_match_fail_version_0)
{
	struct ec_params_fp_mode params = { .mode = FP_MODE_MATCH };
	struct ec_response_fp_mode response{};

	zassert_equal(establish_session(), EC_SUCCESS);
	set_test_fp_context();

	zassert_equal(test_send_host_command(EC_CMD_FP_MODE, 0, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_confirm_template_success)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_params_fp_confirm_template params = { 0 };

	zassert_equal(establish_session(session_key), EC_SUCCESS);
	set_test_fp_context();

	global_context.templ_valid = 1;
	global_context.template_newly_enrolled = global_context.templ_valid;

	generate_and_sign_challenge(session_key, kTestUserId,
				    kSenderFingerGuard, kOpEnrollFinish,
				    params.mac);

	zassert_equal(test_send_host_command(EC_CMD_FP_CONFIRM_TEMPLATE, 0,
					     &params, sizeof(params), nullptr,
					     0),
		      EC_RES_SUCCESS);

	zassert_equal(global_context.templ_valid, 2);
}

ZTEST(fpsensor_auth_commands, test_fp_confirm_template_fail_invalid_signature)
{
	struct ec_response_fp_generate_challenge challenge_response{};
	struct ec_params_fp_confirm_template params = { 0 };

	zassert_equal(establish_session(), EC_SUCCESS);
	set_test_fp_context();

	global_context.templ_valid = 1;
	global_context.template_newly_enrolled = global_context.templ_valid;

	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					     nullptr, 0, &challenge_response,
					     sizeof(challenge_response)),
		      EC_RES_SUCCESS);

	zassert_equal(test_send_host_command(EC_CMD_FP_CONFIRM_TEMPLATE, 0,
					     &params, sizeof(params), nullptr,
					     0),
		      EC_RES_ACCESS_DENIED);

	zassert_equal(global_context.templ_valid, 1);
}

ZTEST(fpsensor_auth_commands, test_fp_confirm_template_fail_state_mismatch)
{
	struct ec_params_fp_confirm_template params = { 0 };

	zassert_equal(establish_session(), EC_SUCCESS);
	set_test_fp_context();

	global_context.templ_valid = 1;
	global_context.template_newly_enrolled = 0;

	zassert_equal(test_send_host_command(EC_CMD_FP_CONFIRM_TEMPLATE, 0,
					     &params, sizeof(params), nullptr,
					     0),
		      EC_RES_ERROR);

	zassert_equal(global_context.templ_valid, 1);
}

ZTEST(fpsensor_auth_commands, test_fp_sign_match_success)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_params_fp_sign_match params = {
		.challenge = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5, 6, 7,
			       8, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5 },
	};
	struct ec_response_fp_sign_match response{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> expected_signature{};

	zassert_equal(establish_session(session_key), EC_SUCCESS);
	set_test_fp_context();

	global_context.positive_match_secret_state.template_matched = 0;
	global_context.positive_match_secret_state.readable = true;
	global_context.positive_match_secret_state.deadline.val =
		get_time().val + 5 * USEC_PER_SEC;

	zassert_equal(test_send_host_command(EC_CMD_FP_SIGN_MATCH, 0, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_SUCCESS);

	zassert_equal(compute_message_signature(
			      session_key, kTestUserId, kSenderFpmcu, kOpAuth,
			      params.challenge, expected_signature),
		      EC_SUCCESS);

	zassert_mem_equal(response.signature, expected_signature.data(),
			  expected_signature.size());
}

ZTEST(fpsensor_auth_commands, test_fp_sign_match_fail_no_match)
{
	struct ec_params_fp_sign_match params = { 0 };
	struct ec_response_fp_sign_match response{};

	zassert_equal(establish_session(), EC_SUCCESS);
	set_test_fp_context();

	global_context.positive_match_secret_state.template_matched =
		FP_NO_SUCH_TEMPLATE;

	zassert_equal(test_send_host_command(EC_CMD_FP_SIGN_MATCH, 0, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_sign_match_fail_deadline_passed)
{
	struct ec_params_fp_sign_match params = { 0 };
	struct ec_response_fp_sign_match response{};

	zassert_equal(establish_session(), EC_SUCCESS);
	set_test_fp_context();

	global_context.positive_match_secret_state.template_matched = 0;
	global_context.positive_match_secret_state.readable = true;
	global_context.positive_match_secret_state.deadline.val =
		get_time().val - 1;

	zassert_equal(test_send_host_command(EC_CMD_FP_SIGN_MATCH, 0, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_TIMEOUT);
}

ZTEST(fpsensor_auth_commands, test_fp_sign_match_fail_no_session)
{
	struct ec_params_fp_sign_match params = { 0 };
	struct ec_response_fp_sign_match response{};

	zassert_equal(test_send_host_command(EC_CMD_FP_SIGN_MATCH, 0, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_reset_does_not_clear_session)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> session_key{};
	struct ec_params_fp_sign_match params = {
		.challenge = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5, 6, 7,
			       8, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5 },
	};
	struct ec_response_fp_sign_match response{};
	std::array<uint8_t, SHA256_DIGEST_LENGTH> expected_signature{};

	zassert_equal(establish_session(session_key), EC_SUCCESS);
	zassert_true((((int)global_context.fp_encryption_status) &
		      (FP_CONTEXT_STATUS_SESSION_ESTABLISHED)) ==
		     (FP_CONTEXT_STATUS_SESSION_ESTABLISHED));

	fp_reset_and_clear_context();

	zassert_true((((int)global_context.fp_encryption_status) &
		      (FP_CONTEXT_STATUS_SESSION_ESTABLISHED)) ==
		     (FP_CONTEXT_STATUS_SESSION_ESTABLISHED));

	std::ranges::copy(kTestUserId, global_context.user_id.begin());
	global_context.positive_match_secret_state.template_matched = 0;
	global_context.positive_match_secret_state.readable = true;
	global_context.positive_match_secret_state.deadline.val =
		get_time().val + 5 * USEC_PER_SEC;

	zassert_equal(test_send_host_command(EC_CMD_FP_SIGN_MATCH, 0, &params,
					     sizeof(params), &response,
					     sizeof(response)),
		      EC_RES_SUCCESS);

	zassert_equal(compute_message_signature(
			      session_key, kTestUserId, kSenderFpmcu, kOpAuth,
			      params.challenge, expected_signature),
		      EC_SUCCESS);

	zassert_mem_equal(response.signature, expected_signature.data(),
			  expected_signature.size());
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_clears_context)
{
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	uint32_t status;

	constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed_1 = {
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	};
	constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed_2 = {
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	};

	zassert_equal(initialize_pairing_key(pairing_key), EC_SUCCESS);

	/* 1. Establish first session with tpm_seed_1 */
	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL,
					     0, &nonce_response,
					     sizeof(nonce_response)),
		      EC_RES_SUCCESS);

	zassert_equal(generate_valid_establish_session_request(
			      pairing_key, nonce_response.nonce, tpm_seed_1,
			      &session_params),
		      EC_SUCCESS);

	zassert_equal(test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
					     &session_params,
					     sizeof(session_params), NULL, 0),
		      EC_RES_SUCCESS);

	/* Set UserID and some templates */
	set_test_fp_context({ 1, 2, 3, 4, 5, 6, 7, 8 });
	global_context.templ_valid = 1;

	/* Verify current state */
	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);
	zassert_true((((int)status) & (FP_ENC_STATUS_SEED_SET)) ==
		     (FP_ENC_STATUS_SEED_SET));
	zassert_true((((int)status) & (FP_CONTEXT_USER_ID_SET)) ==
		     (FP_CONTEXT_USER_ID_SET));
	zassert_equal(global_context.templ_valid, 1u);
	zassert_mem_equal(global_context.tpm_seed.data(), tpm_seed_1.data(),
			  tpm_seed_1.size());

	/* 2. Establish second session with tpm_seed_2 */
	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL,
					     0, &nonce_response,
					     sizeof(nonce_response)),
		      EC_RES_SUCCESS);

	zassert_equal(generate_valid_establish_session_request(
			      pairing_key, nonce_response.nonce, tpm_seed_2,
			      &session_params),
		      EC_SUCCESS);

	zassert_equal(test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
					     &session_params,
					     sizeof(session_params), NULL, 0),
		      EC_RES_SUCCESS);

	/* 3. Verify that context was cleared and TPM seed updated */
	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);
	zassert_true((((int)status) & (FP_ENC_STATUS_SEED_SET)) ==
		     (FP_ENC_STATUS_SEED_SET));
	/* User ID should be cleared */
	zassert_true((((int)status) & (FP_CONTEXT_USER_ID_SET)) == 0);
	for (uint8_t val : global_context.user_id) {
		zassert_equal(val, 0);
	}

	/* Templates should be cleared */
	zassert_equal(global_context.templ_valid, 0u);

	/* TPM seed should be updated to tpm_seed_2 */
	zassert_mem_equal(global_context.tpm_seed.data(), tpm_seed_2.data(),
			  tpm_seed_2.size());
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_corrupted_seed)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	uint32_t status;
	constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6,
		7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2,
	};

	zassert_equal(initialize_pairing_key(pairing_key), EC_SUCCESS);

	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL,
					     0, &nonce_response,
					     sizeof(nonce_response)),
		      EC_RES_SUCCESS);

	zassert_equal(generate_valid_establish_session_request(
			      pairing_key, nonce_response.nonce, tpm_seed,
			      &session_params),
		      EC_SUCCESS);

	/* Corrupt the encrypted TPM seed */
	session_params.enc_tpm_seed[0] ^= 0xFF;

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	/* Expect failure in TPM Seed decryption. */
	zassert_equal(rv, EC_RES_ERROR);

	/* Verify that TPM seed is NOT set and session is NOT established */
	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);
	zassert_true((((int)status) & (FP_ENC_STATUS_SEED_SET)) == 0);
	zassert_true(
		(((int)status) & (FP_CONTEXT_STATUS_SESSION_ESTABLISHED)) == 0);
}

ZTEST(fpsensor_auth_commands,
      test_fp_command_reestablish_session_corrupted_seed)
{
	enum ec_status rv;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_establish_session session_params;
	uint32_t status;
	constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed_1 = {
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	};
	constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed_2 = {
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	};

	zassert_equal(initialize_pairing_key(pairing_key), EC_SUCCESS);

	/* 1. Establish first session with tpm_seed_1 */
	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL,
					     0, &nonce_response,
					     sizeof(nonce_response)),
		      EC_RES_SUCCESS);

	zassert_equal(generate_valid_establish_session_request(
			      pairing_key, nonce_response.nonce, tpm_seed_1,
			      &session_params),
		      EC_SUCCESS);

	zassert_equal(test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
					     &session_params,
					     sizeof(session_params), NULL, 0),
		      EC_RES_SUCCESS);

	/* Verify current state */
	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);
	zassert_true((((int)status) & (FP_ENC_STATUS_SEED_SET)) ==
		     (FP_ENC_STATUS_SEED_SET));
	zassert_true(
		(((int)status) & (FP_CONTEXT_STATUS_SESSION_ESTABLISHED)) ==
		(FP_CONTEXT_STATUS_SESSION_ESTABLISHED));

	/* 2. Try to re-establish session with corrupted tpm_seed_2 */
	zassert_equal(test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL,
					     0, &nonce_response,
					     sizeof(nonce_response)),
		      EC_RES_SUCCESS);

	zassert_equal(generate_valid_establish_session_request(
			      pairing_key, nonce_response.nonce, tpm_seed_2,
			      &session_params),
		      EC_SUCCESS);

	/* Corrupt the encrypted TPM seed */
	session_params.enc_tpm_seed[0] ^= 0xFF;

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
				    &session_params, sizeof(session_params),
				    NULL, 0);

	/* Expect failure in TPM Seed decryption. */
	zassert_equal(rv, EC_RES_ERROR);

	/* 3. Verify that the previous session is gone and new one is not set */
	zassert_equal(get_fp_encryption_status(&status), EC_SUCCESS);
	zassert_true((((int)status) & (FP_ENC_STATUS_SEED_SET)) == 0);
	zassert_true(
		(((int)status) & (FP_CONTEXT_STATUS_SESSION_ESTABLISHED)) == 0);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_busy)
{
	enum ec_status rv;
	struct ec_params_fp_establish_session session_params = {};
	static constexpr uint32_t modes[] = {
		FP_MODE_ENROLL_SESSION,
		FP_MODE_ENROLL_IMAGE,
		FP_MODE_MATCH,
		FP_MODE_RESET_SENSOR,
		FP_MODE_ENCRYPT_TEMPLATE,
		FP_MODE_DECRYPT_TEMPLATE,
	};

	/* Pretend that the session nonce was generated. */
	global_context.fp_encryption_status |= FP_CONTEXT_SESSION_NONCE_SET;

	for (uint32_t mode : modes) {
		/* Pretend that an operation on templates is running. */
		global_context.sensor_mode = mode;

		rv = test_send_host_command(EC_CMD_FP_ESTABLISH_SESSION, 0,
					    &session_params,
					    sizeof(session_params), NULL, 0);

		zassert_equal(rv, EC_RES_BUSY);
	}

	global_context.sensor_mode = 0;
}

ZTEST(fpsensor_auth_commands, test_fp_command_template_v0_decrypted)
{
	struct test_v0_template_layout params{};

	setup_and_encrypt_v0_template(params, kTestSaltNonTrivial, false);

	zassert_equal(test_send_host_command(EC_CMD_FP_TEMPLATE, 0, &params,
					     sizeof(params), NULL, 0),
		      EC_RES_SUCCESS);

	zassert_equal(global_context.templ_valid, 1u);
}

ZTEST(fpsensor_auth_commands, test_fp_command_template_v0_commit_trivial_salt)
{
	struct test_v0_template_layout params{};

	setup_and_encrypt_v0_template(params, kTestSaltTrivial, false);

	zassert_equal(test_send_host_command(EC_CMD_FP_TEMPLATE, 0, &params,
					     sizeof(params), NULL, 0),
		      EC_RES_INVALID_PARAM);

	zassert_equal(global_context.templ_valid, 0u);
}

ZTEST(fpsensor_auth_commands, test_fp_command_template_v0_commit_v3)
{
	test_v0_template_layout params{};

	setup_and_encrypt_v0_template(params, kTestSaltNonTrivial, false);

	params.metadata.struct_version = 3;

	zassert_equal(test_send_host_command(EC_CMD_FP_TEMPLATE, 0, &params,
					     sizeof(params), NULL, 0),
		      EC_RES_INVALID_PARAM);

	zassert_equal(global_context.templ_valid, 0u);
}

ZTEST(fpsensor_auth_commands, test_fp_command_template_v0_corrupted_tag)
{
	struct test_v0_template_layout params{};

	setup_and_encrypt_v0_template(params, kTestSaltNonTrivial, true);

	zassert_equal(test_send_host_command(EC_CMD_FP_TEMPLATE, 0, &params,
					     sizeof(params), NULL, 0),
		      EC_RES_UNAVAILABLE);

	zassert_equal(global_context.templ_valid, 0u);
}

} // namespace

static void *fpsensor_auth_commands_setup(void)
{
	/* Start shimmed tasks (including FPSENSOR background task) */
	start_ec_tasks();
	k_msleep(100);

	return NULL;
}

static void before_fpsensor_auth_commands(void *fixture)
{
	static const uint8_t fake_rollback_entropy[] = "some_rollback_entropy";
	const struct rollback_data data = {
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

	zassert_ok(fp_sensor_init());
	rollback_add_entropy(fake_rollback_entropy,
			     sizeof(fake_rollback_entropy) - 1);

#ifdef CONFIG_OTP_KEY
	std::ranges::copy(default_fake_otp_key, mock_otp.otp_key_buffer);
#endif

	/*
	 * Reset fingerprint context, session secrets, and sensor mode for a
	 * clean state.
	 */
	fp_reset_and_clear_context();
	reset_session();
	global_context.sensor_mode = 0;
}

ZTEST_SUITE(fpsensor_auth_commands, NULL, fpsensor_auth_commands_setup,
	    before_fpsensor_auth_commands, NULL, NULL);
