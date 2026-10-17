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
#include "fpsensor_test_utils.h"
#include "mock/otpi_mock.h"
#include "openssl/aes.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/evp.h"
#include "openssl/obj_mac.h"
#include "rollback.h"
#include "rollback_private.h"
#include "sha256.h"
#include "util.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

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

constexpr std::array<uint8_t, 4> kTestUserId = { 0x0a, 0x00, 0x00, 0x00 };
constexpr std::array<uint32_t, FP_CONTEXT_USERID_WORDS> kDefaultContextUserId = {
	10, 0, 0, 0, 0, 0, 0, 0
};
constexpr std::array<uint32_t, FP_CONTEXT_USERID_WORDS>
	kSequentialContextUserId = { 0, 1, 2, 3, 4, 5, 6, 7 };
constexpr std::string_view kSenderFingerGuard = "finger_guard";
constexpr std::string_view kSenderFpmcu = "fpmcu";
constexpr std::string_view kOpEnroll = "enroll";
constexpr std::string_view kOpAuth = "auth";
constexpr std::string_view kOpEnrollFinish = "enroll_finish";

struct TemplateEncryptionOptions {
	uint16_t struct_version = FP_TEMPLATE_FORMAT_VERSION;
	bool include_salt = true;
	uint8_t salt_fill = kTestSaltNonTrivial;
	bool encrypt = true;
	bool corrupt_tag = false;
};

size_t populate_encrypted_template(enc_buffer &enc,
				   const TemplateEncryptionOptions &opts)
{
	const size_t payload_size =
		sizeof(enc.fp_template) +
		(opts.include_salt ? sizeof(enc.positive_match_salt) : 0);

	enc.metadata.struct_version = opts.struct_version;
	std::ranges::fill(enc.fp_template, kTemplateFillByte);
	if (opts.include_salt) {
		std::ranges::fill(enc.positive_match_salt, opts.salt_fill);
	}

	if (opts.encrypt) {
		const auto info = encrypt_data_in_place(
			1, global_context.crypto.user_id,
			global_context.crypto.tpm_seed,
			enc.payload().first(payload_size));
		zassert_true(info.has_value());

		std::ranges::copy(info->nonce, enc.metadata.nonce);
		std::ranges::copy(info->encryption_salt,
				  enc.metadata.encryption_salt);
		std::ranges::copy(info->tag, enc.metadata.tag);

		if (opts.corrupt_tag) {
			enc.metadata.tag[0] ^= 0x01;
		}
	}

	return sizeof(enc.metadata) + payload_size;
}

void setup_and_encrypt_v0_template(
	FpFullTemplatePacket<ec_params_fp_template> &params, uint8_t salt_fill,
	bool corrupt_tag, bool encrypt = true)
{
	if (encrypt) {
		set_default_tpm_seed();
	}

	reset_test_fp_context(kSequentialContextUserId);

	params.header()->offset = 0;
	params.header()->size =
		FpTemplateV0Size{
			.size = sizeof(enc_buffer),
			.commit = true,
		}
			.pack();

	populate_encrypted_template(*params.enc(),
				    {
					    .salt_fill = salt_fill,
					    .encrypt = encrypt,
					    .corrupt_tag = corrupt_tag,
				    });
}

uint32_t get_fp_encryption_status()
{
	ec_response_fp_encryption_status resp{};

	zassert_equal(send_host_cmd_resp(EC_CMD_FP_ENC_STATUS, 0, resp),
		      EC_RES_SUCCESS);

	return resp.status;
}

ec_status wait_for_template_decrypt_result()
{
	ec_params_fp_template_v1 result_params = {
		.cmd = FP_TEMPLATE_GET_RESULT,
	};
	ec_status res;
	int timeout = 50; /* Poll up to 500ms */

	do {
		k_msleep(10);
		res = send_host_cmd_req(EC_CMD_FP_TEMPLATE, 1, result_params);
	} while (res == EC_RES_BUSY && --timeout > 0);

	return res;
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_pairing_key_keygen)
{
	ec_status rv;
	ec_response_fp_establish_pairing_key_keygen keygen_response{};

	rv = send_host_cmd_resp(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				keygen_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	const auto pubkey = create_ec_key_from_pubkey(keygen_response.pubkey);

	zassert_true(pubkey.has_value());
	zassert_equal(EC_KEY_check_key(pubkey->get()), 1);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_and_load_pairing_key)
{
	ec_status rv;
	ec_response_fp_establish_pairing_key_keygen keygen_response{};
	ec_params_fp_establish_pairing_key_wrap wrap_params{
		.peers_pubkey = kTestPeerPubKey,
	};
	ec_response_fp_establish_pairing_key_wrap wrap_response{};
	ec_params_fp_load_pairing_key load_params{};

	rv = send_host_cmd_resp(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				keygen_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	rv = send_host_cmd(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0, wrap_params,
			   wrap_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	load_params.encrypted_pairing_key = wrap_response.encrypted_pairing_key;

	rv = send_host_cmd_req(EC_CMD_FP_LOAD_PAIRING_KEY, 0, load_params);

	zassert_equal(rv, EC_RES_SUCCESS);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_pairing_key_fail)
{
	ec_status rv;
	ec_params_fp_establish_pairing_key_wrap wrap_params{
		.peers_pubkey = kTestPeerPubKey,
	};
	ec_response_fp_establish_pairing_key_wrap wrap_response{};

	rv = send_host_cmd(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0, wrap_params,
			   wrap_response);

	zassert_not_equal(rv, EC_RES_SUCCESS);
}

ZTEST(fpsensor_auth_commands, test_fp_command_load_pairing_key_invalid)
{
	ec_status rv;
	ec_response_fp_establish_pairing_key_keygen keygen_response{};
	ec_params_fp_establish_pairing_key_wrap wrap_params{
		.peers_pubkey = kTestPeerPubKey,
	};
	ec_response_fp_establish_pairing_key_wrap wrap_response{};
	ec_params_fp_load_pairing_key load_params{};

	rv = send_host_cmd_resp(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				keygen_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	rv = send_host_cmd(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0, wrap_params,
			   wrap_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	/*
	 * Copy full structure, then clear encryption info to simulate invalid
	 * state.
	 */
	load_params.encrypted_pairing_key = wrap_response.encrypted_pairing_key;
	load_params.encrypted_pairing_key.info = {};

	rv = send_host_cmd_req(EC_CMD_FP_LOAD_PAIRING_KEY, 0, load_params);

	zassert_equal(rv, EC_RES_UNAVAILABLE);
}

ZTEST(fpsensor_auth_commands, test_fp_command_generate_nonce)
{
	ec_status rv;
	ec_response_fp_generate_nonce nonce_response{};

	rv = send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0, nonce_response);

	zassert_equal(rv, EC_RES_SUCCESS);
}

std::array<uint8_t, FP_PAIRING_KEY_LEN> initialize_pairing_key()
{
	ec_status rv;
	ec_response_fp_establish_pairing_key_keygen keygen_response{};
	ec_params_fp_establish_pairing_key_wrap wrap_params{};
	ec_response_fp_establish_pairing_key_wrap wrap_response{};
	ec_params_fp_load_pairing_key load_params{};

	/* Ask FPMCU for its public key */
	rv = send_host_cmd_resp(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				keygen_response);
	zassert_equal(rv, EC_RES_SUCCESS);

	/* Convert FPMCU public key to more useful form */
	const auto fpmcu_public_key =
		create_ec_key_from_pubkey(keygen_response.pubkey);
	zassert_true(fpmcu_public_key.has_value());

	/* Generate ECDH private key on our side */
	bssl::UniquePtr<EC_KEY> ecdh_key = generate_elliptic_curve_key();
	zassert_not_equal(ecdh_key.get(), nullptr);

	/* Get public key from that key */
	const auto pubkey = create_pubkey_from_ec_key(*ecdh_key);
	zassert_true(pubkey.has_value());

	wrap_params.peers_pubkey = *pubkey;

	/* Generate Pairing Key on our side */
	const auto generated_key = generate_ecdh_shared_secret_without_kdf(
		*ecdh_key, **fpmcu_public_key);
	zassert_true(generated_key.has_value());

	/*
	 * Send our public key to the FPMCU. FPMCU will return encrypted
	 * Pairing Key.
	 */
	rv = send_host_cmd(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0, wrap_params,
			   wrap_response);
	zassert_equal(rv, EC_RES_SUCCESS);

	load_params.encrypted_pairing_key = wrap_response.encrypted_pairing_key;

	/* Load Pairing Key to the FPMCU */
	rv = send_host_cmd_req(EC_CMD_FP_LOAD_PAIRING_KEY, 0, load_params);
	zassert_equal(rv, EC_RES_SUCCESS);

	return *generated_key;
}

ec_params_fp_establish_session generate_valid_establish_session_request(
	std::span<const uint8_t, FP_PAIRING_KEY_LEN> pairing_key,
	std::span<const uint8_t, FP_CK_SESSION_NONCE_LEN> fpmcu_nonce,
	std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed =
		kDefaultTpmSeed)
{
	ec_params_fp_establish_session session_params{};
	const auto aad = as_operation_span("tpm_seed");

	/* Get our session nonce */
	constexpr std::array<uint8_t, FP_CK_SESSION_NONCE_LEN> session_nonce = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	/* Obtain session key on our side */
	const auto session_key = generate_session_key(
		fpmcu_nonce, session_nonce, pairing_key, {});
	zassert_true(session_key.has_value());

	zassert_equal(tpm_seed.size(), sizeof(session_params.enc_tpm_seed));

	constexpr std::array<uint8_t, FP_AES_KEY_NONCE_BYTES> nonce = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};
	zassert_equal(nonce.size(), sizeof(session_params.nonce));
	std::ranges::copy(nonce, session_params.nonce);

	/* Encrypt tpm_seed using session key */
	bssl::ScopedEVP_AEAD_CTX ctx;
	int aead_ret = EVP_AEAD_CTX_init(ctx.get(), EVP_aead_aes_256_gcm(),
					 session_key->data(),
					 session_key->size(),
					 sizeof(session_params.tag), nullptr);
	zassert_equal(aead_ret, 1);

	size_t tag_bytes_written = 0;
	aead_ret = EVP_AEAD_CTX_seal_scatter(
		ctx.get(), session_params.enc_tpm_seed, session_params.tag,
		&tag_bytes_written, sizeof(session_params.tag),
		session_params.nonce, sizeof(session_params.nonce),
		tpm_seed.data(), tpm_seed.size(), /* extra_in = */ nullptr,
		/* extra_in_len = */ 0, aad.data(), aad.size());
	zassert_equal(aead_ret, 1);
	zassert_equal(tag_bytes_written, sizeof(session_params.tag));

	/* Copy our session nonce to the structure */
	std::ranges::copy(session_nonce, session_params.peer_nonce);

	return session_params;
}

std::array<uint8_t, SHA256_DIGEST_LENGTH>
establish_session(std::span<const uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed =
			  kDefaultTpmSeed)
{
	const auto pairing_key = initialize_pairing_key();

	ec_response_fp_generate_nonce nonce_response{};

	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0,
					 nonce_response),
		      EC_RES_SUCCESS);

	constexpr std::array<uint8_t, FP_CK_SESSION_NONCE_LEN> peer_nonce = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
	};

	const auto generated_session_key = generate_session_key(
		nonce_response.nonce, peer_nonce, pairing_key, {});
	zassert_true(generated_session_key.has_value());

	const auto session_params = generate_valid_establish_session_request(
		pairing_key, nonce_response.nonce, tpm_seed);

	zassert_equal(send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0,
					session_params),
		      EC_RES_SUCCESS);

	return *generated_session_key;
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session)
{
	ec_status rv;
	ec_response_fp_generate_nonce nonce_response{};

	const auto pairing_key = initialize_pairing_key();

	zassert_true((get_fp_encryption_status() & FP_ENC_STATUS_SEED_SET) ==
		     0);

	global_context.templates.templ_valid = 1;

	rv = send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0, nonce_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	zassert_true((get_fp_encryption_status() & FP_ENC_STATUS_SEED_SET) ==
		     0);

	const auto session_params = generate_valid_establish_session_request(
		pairing_key, nonce_response.nonce);

	rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0, session_params);

	zassert_equal(rv, EC_RES_SUCCESS);

	zassert_true((get_fp_encryption_status() & FP_ENC_STATUS_SEED_SET) ==
		     FP_ENC_STATUS_SEED_SET);

	zassert_equal(global_context.templates.templ_valid, 0u);
}

ZTEST(fpsensor_auth_commands,
      test_fp_command_establish_session_fail_different_pk)
{
	ec_status rv;
	ec_response_fp_generate_nonce nonce_response{};

	const auto pairing_key = initialize_pairing_key();

	rv = send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0, nonce_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	const auto session_params = generate_valid_establish_session_request(
		pairing_key, nonce_response.nonce);

	// Change Pairing Key to different one.
	initialize_pairing_key();

	// Try to establish session using request prepared for previous
	// Pairing Key.
	rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0, session_params);

	// Expect failure in TPM Seed decryption.
	zassert_equal(rv, EC_RES_ERROR);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_deny)
{
	ec_status rv;
	ec_response_fp_generate_nonce nonce_response{};
	ec_params_fp_establish_session session_params{};

	const auto pairing_key = initialize_pairing_key();

	// Establish session without generate nonce should fail.
	rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0, session_params);

	zassert_equal(rv, EC_RES_ACCESS_DENIED);

	rv = send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0, nonce_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	session_params = generate_valid_establish_session_request(
		pairing_key, nonce_response.nonce);

	rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0, session_params);

	zassert_equal(rv, EC_RES_SUCCESS);
}

ZTEST(fpsensor_auth_commands,
      test_fp_command_establish_session_limit_without_generated_nonce)
{
	ec_status rv;
	ec_params_fp_establish_session session_params{};

	initialize_pairing_key();

	/* Call nonce context without generated nonce should fail. */
	rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0, session_params);

	zassert_equal(rv, EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands,
      test_fp_command_establish_session_limit_normal_context)
{
	ec_status rv;
	ec_response_fp_generate_nonce nonce_response{};

	const auto pairing_key = initialize_pairing_key();

	rv = send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0, nonce_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	/* Normal context should not clear the generated nonce. */
	/* This will be used for the migration path. */
	reset_test_fp_context(kDefaultContextUserId);

	const auto session_params = generate_valid_establish_session_request(
		pairing_key, nonce_response.nonce);

	/* Call nonce context with generated nonce should success. */
	rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0, session_params);

	zassert_equal(rv, EC_RES_SUCCESS);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_limit_twice_1)
{
	ec_status rv;
	ec_response_fp_generate_nonce nonce_response{};

	const auto pairing_key = initialize_pairing_key();

	rv = send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0, nonce_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	const auto session_params = generate_valid_establish_session_request(
		pairing_key, nonce_response.nonce);

	rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0, session_params);

	zassert_equal(rv, EC_RES_SUCCESS);

	/* Call nonce context twice should fail. */
	rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0, session_params);

	zassert_equal(rv, EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_limit_twice_2)
{
	ec_status rv;
	ec_response_fp_generate_nonce nonce_response{};

	const auto pairing_key = initialize_pairing_key();

	rv = send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0, nonce_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	rv = send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0, nonce_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	const auto session_params = generate_valid_establish_session_request(
		pairing_key, nonce_response.nonce);

	rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0, session_params);

	zassert_equal(rv, EC_RES_SUCCESS);

	/*
	 * Call nonce context twice should fail even if two nonces were
	 * generated.
	 */
	rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0, session_params);

	zassert_equal(rv, EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_load_pk)
{
	ec_status rv;
	ec_response_fp_generate_nonce nonce_response{};
	ec_response_fp_establish_pairing_key_keygen keygen_response{};
	ec_params_fp_establish_pairing_key_wrap wrap_params{
		.peers_pubkey = kTestPeerPubKey,
	};
	ec_response_fp_establish_pairing_key_wrap wrap_response{};
	ec_params_fp_load_pairing_key load_params{};

	const auto pairing_key = initialize_pairing_key();

	rv = send_host_cmd_resp(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				keygen_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	rv = send_host_cmd(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0, wrap_params,
			   wrap_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	load_params.encrypted_pairing_key = wrap_response.encrypted_pairing_key;

	rv = send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0, nonce_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	rv = send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0, nonce_response);

	zassert_equal(rv, EC_RES_SUCCESS);

	const auto session_params = generate_valid_establish_session_request(
		pairing_key, nonce_response.nonce);

	rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0, session_params);

	zassert_equal(rv, EC_RES_SUCCESS);

	/* Pairing key can be loaded after the session was established. */
	rv = send_host_cmd_req(EC_CMD_FP_LOAD_PAIRING_KEY, 0, load_params);

	zassert_equal(rv, EC_RES_SUCCESS);
}

void load_v1_template(const TemplateEncryptionOptions &opts = {})
{
	FpFullTemplatePacket<ec_params_fp_template_v1> params{};
	const size_t data_size =
		populate_encrypted_template(*params.enc(), opts);

	params.header()->offset = 0;
	params.header()->size = static_cast<uint32_t>(data_size);
	params.header()->cmd = FP_TEMPLATE_LOAD;

	zassert_equal(test_send_host_command(EC_CMD_FP_TEMPLATE, 1, &params,
					     params.packet_size(data_size),
					     nullptr, 0),
		      EC_RES_SUCCESS);
}

ZTEST(fpsensor_auth_commands, test_fp_command_template_v1_decrypted)
{
	ec_response_fp_generate_nonce nonce_response{};

	establish_session();

	load_v1_template();

	ec_params_fp_template_v1 decrypt_params = {
		.cmd = FP_TEMPLATE_DECRYPT,
	};
	zassert_equal(send_host_cmd_req(EC_CMD_FP_TEMPLATE, 1, decrypt_params),
		      EC_RES_SUCCESS);

	zassert_equal(wait_for_template_decrypt_result(), EC_RES_SUCCESS);

	(void)get_fp_encryption_status();

	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0,
					 nonce_response),
		      EC_RES_SUCCESS);

	(void)get_fp_encryption_status();
}

/* Test that legacy format (v3) isn't accepted by commit function. */
ZTEST(fpsensor_auth_commands, test_fp_command_template_v1_commit_v3)
{
	establish_session();

	load_v1_template({ .struct_version = 3, .include_salt = false });

	ec_params_fp_template_v1 decrypt_params = {
		.cmd = FP_TEMPLATE_DECRYPT,
	};

	zassert_equal(send_host_cmd_req(EC_CMD_FP_TEMPLATE, 1, decrypt_params),
		      EC_RES_SUCCESS);

	zassert_equal(wait_for_template_decrypt_result(), EC_RES_INVALID_PARAM);
}

/* Test that trivial positive match salt will be detected into an error. */
ZTEST(fpsensor_auth_commands, test_fp_command_template_v1_commit_trivial_salt)
{
	establish_session();
	reset_test_fp_context(kSequentialContextUserId);

	load_v1_template({ .salt_fill = 0x00 });

	ec_params_fp_template_v1 decrypt_params = {
		.cmd = FP_TEMPLATE_DECRYPT,
	};
	zassert_equal(send_host_cmd_req(EC_CMD_FP_TEMPLATE, 1, decrypt_params),
		      EC_RES_SUCCESS);

	zassert_equal(wait_for_template_decrypt_result(), EC_RES_INVALID_PARAM);
}

ZTEST(fpsensor_auth_commands, test_fp_command_template_v1_commit_without_seed)
{
	reset_test_fp_context(kSequentialContextUserId);

	load_v1_template({ .salt_fill = 0x12, .encrypt = false });

	ec_params_fp_template_v1 decrypt_params = {
		.cmd = FP_TEMPLATE_DECRYPT,
	};
	zassert_equal(send_host_cmd_req(EC_CMD_FP_TEMPLATE, 1, decrypt_params),
		      EC_RES_SUCCESS);

	zassert_equal(wait_for_template_decrypt_result(), EC_RES_UNAVAILABLE);
}

std::array<uint8_t, SHA256_DIGEST_LENGTH> generate_and_sign_challenge(
	std::span<const uint8_t, SHA256_DIGEST_LENGTH> session_key,
	std::span<const uint8_t> user_id, std::string_view sender,
	std::string_view operation)
{
	ec_response_fp_generate_challenge challenge_response{};
	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					 challenge_response),
		      EC_RES_SUCCESS);

	const auto sig = compute_message_signature(
		session_key, user_id, sender, operation,
		challenge_response.challenge);
	zassert_true(sig.has_value());
	return *sig;
}

ZTEST(fpsensor_auth_commands, test_fp_command_generate_challenge)
{
	ec_response_fp_generate_challenge challenge_response{};

	establish_session();

	zassert_true((get_fp_encryption_status() & FP_AUTH_CHALLENGE_SET) == 0);

	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					 challenge_response),
		      EC_RES_SUCCESS);

	zassert_true((get_fp_encryption_status() & FP_AUTH_CHALLENGE_SET) ==
		     FP_AUTH_CHALLENGE_SET);
}

ZTEST(fpsensor_auth_commands,
      test_fp_command_generate_challenge_fail_no_session)
{
	ec_response_fp_generate_challenge challenge_response{};

	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					 challenge_response),
		      EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_validate_request)
{
	const auto session_key = establish_session();

	const auto mac = generate_and_sign_challenge(
		session_key, kTestUserId, kSenderFingerGuard, kOpEnroll);

	zassert_true((get_fp_encryption_status() & FP_AUTH_CHALLENGE_SET) ==
		     FP_AUTH_CHALLENGE_SET);

	zassert_true(validate_request(kTestUserId, kOpEnroll, mac).has_value());

	zassert_true((get_fp_encryption_status() & FP_AUTH_CHALLENGE_SET) == 0);
}

ZTEST(fpsensor_auth_commands, test_fp_validate_request_fail_no_challenge)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac{};

	establish_session();

	zassert_equal(validate_request(kTestUserId, kOpEnroll, mac).error(),
		      EC_ERROR_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_validate_request_fail_invalid_signature)
{
	std::array<uint8_t, SHA256_DIGEST_LENGTH> mac{};
	ec_response_fp_generate_challenge challenge_response{};

	establish_session();

	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					 challenge_response),
		      EC_RES_SUCCESS);

	zassert_equal(validate_request(kTestUserId, kOpEnroll, mac).error(),
		      EC_ERROR_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_validate_request_fail_timeout)
{
	ec_response_fp_generate_challenge challenge_response{};

	const auto session_key = establish_session();

	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					 challenge_response),
		      EC_RES_SUCCESS);

	global_context.crypto.active_challenge->deadline = {};

	const auto mac = compute_message_signature(
		session_key, kTestUserId, kSenderFingerGuard, kOpEnroll,
		challenge_response.challenge);
	zassert_true(mac.has_value());

	zassert_equal(validate_request(kTestUserId, kOpEnroll, *mac).error(),
		      EC_ERROR_TIMEOUT);
}

ZTEST(fpsensor_auth_commands, test_fp_sign_message)
{
	constexpr std::array<const uint8_t, FP_CHALLENGE_SIZE> challenge = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5,
	};

	const auto session_key = establish_session();

	const auto mac = global_context.crypto.sign_message(
		kTestUserId, kOpEnroll, challenge);
	zassert_true(mac.has_value());

	const auto expected_mac = compute_message_signature(
		session_key, kTestUserId, kSenderFpmcu, kOpEnroll, challenge);
	zassert_true(expected_mac.has_value());

	zassert_true(std::ranges::equal(*mac, *expected_mac));
}

ZTEST(fpsensor_auth_commands, test_fp_sign_message_fail_no_session)
{
	constexpr std::array<const uint8_t, FP_CHALLENGE_SIZE> challenge = {};

	zassert_equal(global_context.crypto
			      .sign_message(kTestUserId, kOpEnroll, challenge)
			      .error(),
		      EC_ERROR_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_mode_match_correct_signature)
{
	ec_params_fp_mode_v1 params = { .mode = FP_MODE_MATCH };
	ec_response_fp_mode response{};

	const auto session_key = establish_session();
	reset_test_fp_context(kDefaultContextUserId);

	std::ranges::copy(generate_and_sign_challenge(session_key, kTestUserId,
						      kSenderFingerGuard,
						      kOpAuth),
			  params.mac);

	zassert_equal(send_host_cmd(EC_CMD_FP_MODE, 1, params, response),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode, FP_MODE_MATCH);
}

ZTEST(fpsensor_auth_commands, test_fp_mode_disable_match_no_signature)
{
	ec_params_fp_mode_v1 params = { .mode = 0 };
	ec_response_fp_mode response{};

	establish_session();

	global_context.sensor_mode = FP_MODE_MATCH;

	zassert_equal(send_host_cmd(EC_CMD_FP_MODE, 1, params, response),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode, 0u, "%x");
}

ZTEST(fpsensor_auth_commands, test_fp_mode_match_invalid_signature)
{
	ec_response_fp_generate_challenge challenge_response{};
	ec_params_fp_mode_v1 params = { .mode = FP_MODE_MATCH };
	ec_response_fp_mode response{};

	establish_session();
	reset_test_fp_context(kDefaultContextUserId);

	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					 challenge_response),
		      EC_RES_SUCCESS);

	zassert_equal(send_host_cmd(EC_CMD_FP_MODE, 1, params, response),
		      EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_mode_enroll_correct_signature)
{
	ec_params_fp_mode_v1 params = { .mode = FP_MODE_ENROLL_SESSION |
						FP_MODE_ENROLL_IMAGE };
	ec_response_fp_mode response{};

	const auto session_key = establish_session();
	reset_test_fp_context(kDefaultContextUserId);

	std::ranges::copy(generate_and_sign_challenge(session_key, kTestUserId,
						      kSenderFingerGuard,
						      kOpEnroll),
			  params.mac);

	zassert_equal(send_host_cmd(EC_CMD_FP_MODE, 1, params, response),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode,
		      FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE);
}

ZTEST(fpsensor_auth_commands, test_fp_mode_disable_enroll_no_signature)
{
	ec_params_fp_mode_v1 params = { .mode = 0 };
	ec_response_fp_mode response{};

	establish_session();

	global_context.sensor_mode = FP_MODE_ENROLL_SESSION;

	zassert_equal(send_host_cmd(EC_CMD_FP_MODE, 1, params, response),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode, 0u, "%x");
}

ZTEST(fpsensor_auth_commands, test_fp_mode_enroll_invalid_signature)
{
	ec_response_fp_generate_challenge challenge_response{};
	ec_params_fp_mode_v1 params = { .mode = FP_MODE_ENROLL_SESSION |
						FP_MODE_ENROLL_IMAGE };
	ec_response_fp_mode response{};

	establish_session();
	reset_test_fp_context(kDefaultContextUserId);

	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					 challenge_response),
		      EC_RES_SUCCESS);

	zassert_equal(send_host_cmd(EC_CMD_FP_MODE, 1, params, response),
		      EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_mode_enroll_session_transitions)
{
	ec_params_fp_mode_v1 params = { .mode = FP_MODE_ENROLL_SESSION |
						FP_MODE_ENROLL_IMAGE };
	ec_response_fp_mode response{};

	const auto session_key = establish_session();
	reset_test_fp_context(kDefaultContextUserId);

	std::ranges::copy(generate_and_sign_challenge(session_key, kTestUserId,
						      kSenderFingerGuard,
						      kOpEnroll),
			  params.mac);

	zassert_equal(send_host_cmd(EC_CMD_FP_MODE, 1, params, response),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode,
		      FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE);

	params.mode = FP_MODE_ENROLL_SESSION | FP_MODE_FINGER_UP;
	std::ranges::fill(params.mac, 0);
	global_context.sensor_mode = FP_MODE_ENROLL_SESSION;

	zassert_equal(send_host_cmd(EC_CMD_FP_MODE, 1, params, response),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode,
		      FP_MODE_ENROLL_SESSION | FP_MODE_FINGER_UP);

	params.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE;
	global_context.sensor_mode = FP_MODE_ENROLL_SESSION;

	zassert_equal(send_host_cmd(EC_CMD_FP_MODE, 1, params, response),
		      EC_RES_SUCCESS);

	zassert_equal(response.mode,
		      FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE);
}

ZTEST(fpsensor_auth_commands, test_fp_mode_match_fail_version_0)
{
	ec_params_fp_mode params = { .mode = FP_MODE_MATCH };
	ec_response_fp_mode response{};

	establish_session();
	reset_test_fp_context(kDefaultContextUserId);

	zassert_equal(send_host_cmd(EC_CMD_FP_MODE, 0, params, response),
		      EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_confirm_template_success)
{
	ec_params_fp_confirm_template params{};

	const auto session_key = establish_session();
	reset_test_fp_context(kDefaultContextUserId);

	global_context.templates.templ_valid = 1;
	global_context.templates.template_newly_enrolled =
		*FpTemplateId::create(global_context.templates.templ_valid);

	std::ranges::copy(generate_and_sign_challenge(session_key, kTestUserId,
						      kSenderFingerGuard,
						      kOpEnrollFinish),
			  params.mac);

	zassert_equal(send_host_cmd_req(EC_CMD_FP_CONFIRM_TEMPLATE, 0, params),
		      EC_RES_SUCCESS);

	zassert_equal(global_context.templates.templ_valid, 2);
}

ZTEST(fpsensor_auth_commands, test_fp_confirm_template_fail_invalid_signature)
{
	ec_response_fp_generate_challenge challenge_response{};
	ec_params_fp_confirm_template params{};

	establish_session();
	reset_test_fp_context(kDefaultContextUserId);

	global_context.templates.templ_valid = 1;
	global_context.templates.template_newly_enrolled =
		*FpTemplateId::create(global_context.templates.templ_valid);

	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_CHALLENGE, 0,
					 challenge_response),
		      EC_RES_SUCCESS);

	zassert_equal(send_host_cmd_req(EC_CMD_FP_CONFIRM_TEMPLATE, 0, params),
		      EC_RES_ACCESS_DENIED);

	zassert_equal(global_context.templates.templ_valid, 1);
}

ZTEST(fpsensor_auth_commands, test_fp_confirm_template_fail_state_mismatch)
{
	ec_params_fp_confirm_template params{};

	establish_session();
	reset_test_fp_context(kDefaultContextUserId);

	global_context.templates.templ_valid = 1;
	global_context.templates.template_newly_enrolled =
		*FpTemplateId::create(0);

	zassert_equal(send_host_cmd_req(EC_CMD_FP_CONFIRM_TEMPLATE, 0, params),
		      EC_RES_ERROR);

	zassert_equal(global_context.templates.templ_valid, 1);
}

ZTEST(fpsensor_auth_commands, test_fp_sign_match_success)
{
	ec_params_fp_sign_match params = {
		.challenge = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5, 6, 7,
			       8, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5 },
	};
	ec_response_fp_sign_match response{};

	const auto session_key = establish_session();
	reset_test_fp_context(kDefaultContextUserId);

	global_context.templates.positive_match_secret_state.enable(
		*FpTemplateId::create(0));
	global_context.templates.positive_match_secret_state.deadline =
		FpClock::now() + std::chrono::seconds(5);

	zassert_equal(send_host_cmd(EC_CMD_FP_SIGN_MATCH, 0, params, response),
		      EC_RES_SUCCESS);

	const auto expected_signature = compute_message_signature(
		session_key, kTestUserId, kSenderFpmcu, kOpAuth,
		params.challenge);
	zassert_true(expected_signature.has_value());

	zassert_true(
		std::ranges::equal(response.signature, *expected_signature));
}

ZTEST(fpsensor_auth_commands, test_fp_sign_match_fail_no_match)
{
	ec_params_fp_sign_match params{};
	ec_response_fp_sign_match response{};

	establish_session();
	reset_test_fp_context(kDefaultContextUserId);

	global_context.templates.positive_match_secret_state.disable();

	zassert_equal(send_host_cmd(EC_CMD_FP_SIGN_MATCH, 0, params, response),
		      EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_sign_match_fail_deadline_passed)
{
	ec_params_fp_sign_match params{};
	ec_response_fp_sign_match response{};

	establish_session();
	reset_test_fp_context(kDefaultContextUserId);

	global_context.templates.positive_match_secret_state.enable(
		*FpTemplateId::create(0));
	global_context.templates.positive_match_secret_state.deadline =
		FpClock::now() - std::chrono::microseconds(1);

	zassert_equal(send_host_cmd(EC_CMD_FP_SIGN_MATCH, 0, params, response),
		      EC_RES_TIMEOUT);
}

ZTEST(fpsensor_auth_commands, test_fp_sign_match_fail_no_session)
{
	ec_params_fp_sign_match params{};
	ec_response_fp_sign_match response{};

	zassert_equal(send_host_cmd(EC_CMD_FP_SIGN_MATCH, 0, params, response),
		      EC_RES_ACCESS_DENIED);
}

ZTEST(fpsensor_auth_commands, test_fp_reset_does_not_clear_session)
{
	ec_params_fp_sign_match params = {
		.challenge = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5, 6, 7,
			       8, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 5 },
	};
	ec_response_fp_sign_match response{};

	const auto session_key = establish_session();
	zassert_true(global_context.crypto.auth_enabled());

	global_context.reset_and_clear_context();

	zassert_true(global_context.crypto.auth_enabled());

	std::ranges::copy(kTestUserId, global_context.crypto.user_id.begin());
	global_context.templates.positive_match_secret_state.enable(
		*FpTemplateId::create(0));
	global_context.templates.positive_match_secret_state.deadline =
		FpClock::now() + std::chrono::seconds(5);

	zassert_equal(send_host_cmd(EC_CMD_FP_SIGN_MATCH, 0, params, response),
		      EC_RES_SUCCESS);

	const auto expected_signature = compute_message_signature(
		session_key, kTestUserId, kSenderFpmcu, kOpAuth,
		params.challenge);
	zassert_true(expected_signature.has_value());

	zassert_true(
		std::ranges::equal(response.signature, *expected_signature));
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_clears_context)
{
	ec_response_fp_generate_nonce nonce_response{};
	uint32_t status;

	constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed_1 = {
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	};
	constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed_2 = {
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	};

	const auto pairing_key = initialize_pairing_key();

	/* 1. Establish first session with tpm_seed_1 */
	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0,
					 nonce_response),
		      EC_RES_SUCCESS);

	auto session_params = generate_valid_establish_session_request(
		pairing_key, nonce_response.nonce, tpm_seed_1);

	zassert_equal(send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0,
					session_params),
		      EC_RES_SUCCESS);

	/* Set UserID and some templates */
	reset_test_fp_context(std::array<uint32_t, FP_CONTEXT_USERID_WORDS>{
		1, 2, 3, 4, 5, 6, 7, 8 });
	global_context.templates.templ_valid = 1;

	/* Verify current state */
	status = get_fp_encryption_status();
	zassert_true((status & FP_ENC_STATUS_SEED_SET) ==
		     FP_ENC_STATUS_SEED_SET);
	zassert_true((status & FP_CONTEXT_USER_ID_SET) ==
		     FP_CONTEXT_USER_ID_SET);
	zassert_equal(global_context.templates.templ_valid, 1u);
	zassert_true(
		std::ranges::equal(global_context.crypto.tpm_seed, tpm_seed_1));

	/* 2. Establish second session with tpm_seed_2 */
	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0,
					 nonce_response),
		      EC_RES_SUCCESS);

	session_params = generate_valid_establish_session_request(
		pairing_key, nonce_response.nonce, tpm_seed_2);

	zassert_equal(send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0,
					session_params),
		      EC_RES_SUCCESS);

	/* 3. Verify that context was cleared and TPM seed updated */
	status = get_fp_encryption_status();
	zassert_true((status & FP_ENC_STATUS_SEED_SET) ==
		     FP_ENC_STATUS_SEED_SET);
	/* User ID should be cleared */
	zassert_true((status & FP_CONTEXT_USER_ID_SET) == 0);
	zassert_true(bytes_are_trivial(global_context.crypto.user_id));

	/* Templates should be cleared */
	zassert_equal(global_context.templates.templ_valid, 0u);

	/* TPM seed should be updated to tpm_seed_2 */
	zassert_true(
		std::ranges::equal(global_context.crypto.tpm_seed, tpm_seed_2));
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_corrupted_seed)
{
	ec_status rv;
	ec_response_fp_generate_nonce nonce_response{};
	uint32_t status;
	constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6,
		7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2,
	};

	const auto pairing_key = initialize_pairing_key();

	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0,
					 nonce_response),
		      EC_RES_SUCCESS);

	auto session_params = generate_valid_establish_session_request(
		pairing_key, nonce_response.nonce, tpm_seed);

	/* Corrupt the encrypted TPM seed */
	session_params.enc_tpm_seed[0] ^= 0xFF;

	rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0, session_params);

	/* Expect failure in TPM Seed decryption. */
	zassert_equal(rv, EC_RES_ERROR);

	/* Verify that TPM seed is NOT set and session is NOT established */
	status = get_fp_encryption_status();
	zassert_true((status & FP_ENC_STATUS_SEED_SET) == 0);
	zassert_true((status & FP_CONTEXT_STATUS_SESSION_ESTABLISHED) == 0);
}

ZTEST(fpsensor_auth_commands,
      test_fp_command_reestablish_session_corrupted_seed)
{
	ec_status rv;
	ec_response_fp_generate_nonce nonce_response{};
	uint32_t status;
	constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed_1 = {
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	};
	constexpr std::array<uint8_t, FP_CONTEXT_TPM_BYTES> tpm_seed_2 = {
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	};

	const auto pairing_key = initialize_pairing_key();

	/* 1. Establish first session with tpm_seed_1 */
	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0,
					 nonce_response),
		      EC_RES_SUCCESS);

	auto session_params = generate_valid_establish_session_request(
		pairing_key, nonce_response.nonce, tpm_seed_1);

	zassert_equal(send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0,
					session_params),
		      EC_RES_SUCCESS);

	/* Verify current state */
	status = get_fp_encryption_status();
	zassert_true((status & FP_ENC_STATUS_SEED_SET) ==
		     FP_ENC_STATUS_SEED_SET);
	zassert_true((status & FP_CONTEXT_STATUS_SESSION_ESTABLISHED) ==
		     FP_CONTEXT_STATUS_SESSION_ESTABLISHED);

	/* 2. Try to re-establish session with corrupted tpm_seed_2 */
	zassert_equal(send_host_cmd_resp(EC_CMD_FP_GENERATE_NONCE, 0,
					 nonce_response),
		      EC_RES_SUCCESS);

	session_params = generate_valid_establish_session_request(
		pairing_key, nonce_response.nonce, tpm_seed_2);

	/* Corrupt the encrypted TPM seed */
	session_params.enc_tpm_seed[0] ^= 0xFF;

	rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0, session_params);

	/* Expect failure in TPM Seed decryption. */
	zassert_equal(rv, EC_RES_ERROR);

	/* 3. Verify that the previous session is gone and new one is not set */
	status = get_fp_encryption_status();
	zassert_true((status & FP_ENC_STATUS_SEED_SET) == 0);
	zassert_true((status & FP_CONTEXT_STATUS_SESSION_ESTABLISHED) == 0);
}

ZTEST(fpsensor_auth_commands, test_fp_command_establish_session_busy)
{
	ec_status rv;
	ec_params_fp_establish_session session_params{};
	static constexpr uint32_t modes[] = {
		FP_MODE_ENROLL_SESSION,
		FP_MODE_ENROLL_IMAGE,
		FP_MODE_MATCH,
		FP_MODE_RESET_SENSOR,
		FP_MODE_ENCRYPT_TEMPLATE,
		FP_MODE_DECRYPT_TEMPLATE,
	};

	/* Pretend that the session nonce was generated. */
	global_context.crypto.session_nonce.emplace();

	for (uint32_t mode : modes) {
		/* Pretend that an operation on templates is running. */
		global_context.sensor_mode = mode;

		rv = send_host_cmd_req(EC_CMD_FP_ESTABLISH_SESSION, 0,
				       session_params);

		zassert_equal(rv, EC_RES_BUSY);
	}

	global_context.sensor_mode = 0;
}

ZTEST(fpsensor_auth_commands, test_fp_command_template_v0_decrypted)
{
	FpFullTemplatePacket<ec_params_fp_template> params{};

	setup_and_encrypt_v0_template(params, kTestSaltNonTrivial, false);

	zassert_equal(send_host_cmd_req(EC_CMD_FP_TEMPLATE, 0, params),
		      EC_RES_SUCCESS);

	zassert_equal(global_context.templates.templ_valid, 1u);
}

ZTEST(fpsensor_auth_commands, test_fp_command_template_v0_commit_trivial_salt)
{
	FpFullTemplatePacket<ec_params_fp_template> params{};

	setup_and_encrypt_v0_template(params, kTestSaltTrivial, false);

	zassert_equal(send_host_cmd_req(EC_CMD_FP_TEMPLATE, 0, params),
		      EC_RES_INVALID_PARAM);

	zassert_equal(global_context.templates.templ_valid, 0u);
}

ZTEST(fpsensor_auth_commands, test_fp_command_template_v0_commit_v3)
{
	FpFullTemplatePacket<ec_params_fp_template> params{};

	setup_and_encrypt_v0_template(params, kTestSaltNonTrivial, false);

	params.enc()->metadata.struct_version = 3;

	zassert_equal(send_host_cmd_req(EC_CMD_FP_TEMPLATE, 0, params),
		      EC_RES_INVALID_PARAM);

	zassert_equal(global_context.templates.templ_valid, 0u);
}

ZTEST(fpsensor_auth_commands, test_fp_command_template_v0_commit_without_seed)
{
	FpFullTemplatePacket<ec_params_fp_template> params{};

	setup_and_encrypt_v0_template(params, kTestSaltNonTrivial, false,
				      /*encrypt=*/false);

	zassert_equal(send_host_cmd_req(EC_CMD_FP_TEMPLATE, 0, params),
		      EC_RES_UNAVAILABLE);

	zassert_equal(global_context.templates.templ_valid, 0u);
}

ZTEST(fpsensor_auth_commands, test_fp_command_template_v0_corrupted_tag)
{
	FpFullTemplatePacket<ec_params_fp_template> params{};

	setup_and_encrypt_v0_template(params, kTestSaltNonTrivial, true);

	zassert_equal(send_host_cmd_req(EC_CMD_FP_TEMPLATE, 0, params),
		      EC_RES_UNAVAILABLE);

	zassert_equal(global_context.templates.templ_valid, 0u);
}

ZTEST(fpsensor_auth_commands, test_fp_info_and_frame_host_cmd_handlers)
{
	FpSensorInfoBuffer info_buf{};

	zassert_ok(get_fp_info_v3(info_buf));

	ec_params_fp_frame frame_v0 = {
		.offset =
			FpFrameV0Offset{
				.buffer_index = FP_FRAME_INDEX_TEMPLATE,
			}
				.pack(),
		.size = 0,
	};
	zassert_equal(ec_cmd_fp_frame(nullptr, &frame_v0,
				      reinterpret_cast<uint8_t *>(&info_buf)),
		      EC_HOST_CMD_UNAVAILABLE);

	ec_params_fp_frame_v1 frame_v1 = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};
	zassert_equal(ec_cmd_fp_frame_v1(nullptr, &frame_v1, nullptr),
		      EC_HOST_CMD_UNAVAILABLE);
}

void *fpsensor_auth_commands_setup()
{
	/* Start shimmed tasks (including FPSENSOR background task) */
	start_ec_tasks();
	k_msleep(100);

	return nullptr;
}

void before_fpsensor_auth_commands(void *fixture)
{
	init_test_rollback([] { zassert_ok(fp_sensor_init()); });

#ifdef CONFIG_OTP_KEY
	std::ranges::copy(default_fake_otp_key, mock_otp.otp_key_buffer);
#endif

	/*
	 * Reset fingerprint context, session secrets, and sensor mode for a
	 * clean state.
	 */
	global_context.reset_and_clear_context();
	global_context.crypto.reset_session();
	global_context.sensor_mode = 0;
}

} /* namespace */

ZTEST_SUITE(fpsensor_auth_commands, nullptr, fpsensor_auth_commands_setup,
	    before_fpsensor_auth_commands, nullptr, nullptr);
