/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if defined(CONFIG_NEWLIB_LIBC) || defined(CONFIG_EXTERNAL_LIBC)
/* For srandom() */
#define _XOPEN_SOURCE 500
#endif

#include "fpsensor_test_utils.h"
#include "mock/fpsensor_crypto_mock.h"
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

#include <cstdlib>
#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>
#include <ec_commands.h>
#include <ec_tasks.h>
#include <flash.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>
#include <mkbp_event.h>
#include <rollback.h>
#include <rollback_private.h>
#include <system.h>

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);
FAKE_VALUE_FUNC(int, system_is_locked);

namespace
{

std::array<uint8_t, IMAGE_SIZE> frame_buffer;

/*
 * How to manually encrypt a template
 *
 * After adding "some_rollback_entropy" entropy to empty rollback secret
 * (32 bytes of 0x00), the secret stored in rollback region is
 * 3ce9c8011d3f98d96fa741da4f10f2f410d80372ebba98ff726b521338e6cfd9
 *
 * The IKM (input key material) is a concatenation of the rollback secret and
 * the TPM seed, so it's
 * 3ce9c8011d3f98d96fa741da4f10f2f410d80372ebba98ff726b521338e6cfd9
 * 766572795f7365637265745f33325f62797465735f6f665f74706d5f73656564
 *
 * UserID is 695f6d5f615f66616b655f757365725f69645f757365645f666f725f74657374
 *
 * To encrypt the template we also need nonce and encryption salt.
 * We get these values from entropy source. In the test environment
 * our entropy source is random(), which is initialized using
 * srandom(0xdeadc0de) in fpsensor_before(), so:
 *
 * The nonce is 941ee3310b891aeba42b2e90
 * Encryption salt is fa253e21359a7920ba1a9868cc26a88f
 *
 * The key used for encryption is HKDF-SHA256(encryption_salt, ikm, user_id)
 * 1a5146988b7e7423fc11c99b5edd6c22
 *
 * The template is appended with positive match salt which is obtained from TRNG
 * in our case it is 64c55d3ee3b3a45ef85bce1ed5caf1e0
 *
 * Finally, we can encrypt (template || positive match salt) with AES-GCM which
 * will give 020dc692bb286c1001e35f17c368053c15c07cd88bf640c897986ddbde23390059
 * ea827f4fee49955c38f0caeb69730e with tag a1ba7aa11e61194bdf7627a72f25d2f8
 */

constexpr std::array<uint8_t, CONFIG_FP_ALGORITHM_TEMPLATE_SIZE>
	kExampleTemplate = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
			     0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
			     0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
			     0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20 };

constexpr ec_fp_template_encryption_metadata kExpectedEncInfo = {
	.struct_version = 4,
	.nonce = { 0x94, 0x1e, 0xe3, 0x31, 0x0b, 0x89, 0x1a, 0xeb, 0xa4, 0x2b,
		   0x2e, 0x90 },
	.encryption_salt = { 0xfa, 0x25, 0x3e, 0x21, 0x35, 0x9a, 0x79, 0x20,
			     0xba, 0x1a, 0x98, 0x68, 0xcc, 0x26, 0xa8, 0x8f },
	.tag = { 0xa1, 0xba, 0x7a, 0xa1, 0x1e, 0x61, 0x19, 0x4b, 0xdf, 0x76,
		 0x27, 0xa7, 0x2f, 0x25, 0xd2, 0xf8 },
};

constexpr std::array<uint8_t, CONFIG_FP_ALGORITHM_TEMPLATE_SIZE +
				      FP_POSITIVE_MATCH_SALT_BYTES>
	kExampleTemplateEncrypted = { 0x02, 0x0d, 0xc6, 0x92, 0xbb, 0x28, 0x6c,
				      0x10, 0x01, 0xe3, 0x5f, 0x17, 0xc3, 0x68,
				      0x05, 0x3c, 0x15, 0xc0, 0x7c, 0xd8, 0x8b,
				      0xf6, 0x40, 0xc8, 0x97, 0x98, 0x6d, 0xdb,
				      0xde, 0x23, 0x39, 0x00, 0x59, 0xea, 0x82,
				      0x7f, 0x4f, 0xee, 0x49, 0x95, 0x5c, 0x38,
				      0xf0, 0xca, 0xeb, 0x69, 0x73, 0x0e };

/*
 * Positive match secret is a HKDF-SHA256(positive_match_salt, ikm, message)
 *
 * The message is a concatenation of string "positive_match_secret for user "
 * and userid. In our case it's 706f7369746976655f6d617463685f7365637265742066
 * 6f72207573657220695f6d5f615f66616b655f757365725f69645f757365645f666f725f74
 * 657374
 *
 * The positive match secret is: 69af2f7a35a249d6584bda3ba9f99f872aa560cce9c4
 * 1f832bf2cdd26e29a76b
 */
constexpr std::array<uint8_t, FP_POSITIVE_MATCH_SECRET_BYTES>
	kExamplePositiveMatchSecret = { 0x69, 0xaf, 0x2f, 0x7a, 0x35, 0xa2,
					0x49, 0xd6, 0x58, 0x4b, 0xda, 0x3b,
					0xa9, 0xf9, 0x9f, 0x87, 0x2a, 0xa5,
					0x60, 0xcc, 0xe9, 0xc4, 0x1f, 0x83,
					0x2b, 0xf2, 0xcd, 0xd2, 0x6e, 0x29,
					0xa7, 0x6b };

int enroll_percent;
int custom_enroll_step(const fingerprint_algorithm *const alg,
		       const uint8_t *const image, int *percent)
{
	*percent = enroll_percent;

	return 0;
}

enc_buffer encrypted_template;

FpSensorInfoBuffer test_info_buffer;

/*
 * Size of params buffer for FP_TEMPLATE command. Its size must be big enough
 * to keep ec_params_fp_template structure and a part of template.
 */
constexpr size_t kTemplateParamsBufferSize = 16;
static_assert(kTemplateParamsBufferSize > sizeof(ec_params_fp_template));

constexpr size_t kTemplateV1ParamsBufferSize = 32;
static_assert(kTemplateV1ParamsBufferSize > sizeof(ec_params_fp_template_v1));

void prepare_encrypted_template(
	const ec_fp_template_encryption_metadata &enc_info = kExpectedEncInfo)
{
	encrypted_template.metadata = enc_info;
	std::ranges::copy(kExampleTemplateEncrypted,
			  encrypted_template.payload().begin());
}

void check_encrypted_template(const enc_buffer &templ)
{
	const auto &enc_info = templ.metadata;
	zassert_equal(enc_info.struct_version, kExpectedEncInfo.struct_version);
	zassert_true(
		std::ranges::equal(enc_info.nonce, kExpectedEncInfo.nonce));
	zassert_true(std::ranges::equal(enc_info.encryption_salt,
					kExpectedEncInfo.encryption_salt));
	zassert_true(std::ranges::equal(enc_info.tag, kExpectedEncInfo.tag));
	zassert_true(
		std::ranges::equal(templ.payload(), kExampleTemplateEncrypted));
}

void load_template_v1_chunks()
{
	FpTemplatePacket<ec_params_fp_template_v1, kTemplateV1ParamsBufferSize>
		params;
	const auto src = encrypted_template.as_bytes();
	size_t offset = 0;

	params.header()->cmd = FP_TEMPLATE_LOAD;
	while (offset < src.size()) {
		params.header()->offset = offset;
		params.header()->size =
			std::min(params.data().size(), src.size() - offset);
		std::ranges::copy(src.subspan(offset, params.header()->size),
				  params.data().begin());

		zassert_ok(ec_cmd_fp_template_v1(
			nullptr, params.header(),
			params.packet_size(params.header()->size)));

		offset += params.header()->size;
	}
}

int load_template_v0_chunks(bool verify_dirty_before_commit = false)
{
	FpTemplatePacket<ec_params_fp_template, kTemplateParamsBufferSize>
		params;
	const auto src = encrypted_template.as_bytes();
	size_t offset = 0;

	while (offset < src.size()) {
		params.header()->offset = offset;
		const uint32_t chunk_size =
			std::min(params.data().size(), src.size() - offset);
		std::ranges::copy(src.subspan(offset, chunk_size),
				  params.data().begin());
		offset += chunk_size;

		const bool commit = (offset == src.size());
		params.header()->size =
			FpTemplateV0Size{
				.size = chunk_size,
				.commit = commit,
			}
				.pack();
		if (commit) {
			return ec_cmd_fp_template(nullptr, params.header(),
						  kTemplateParamsBufferSize);
		}

		zassert_ok(ec_cmd_fp_template(nullptr, params.header(),
					      kTemplateParamsBufferSize));
		if (verify_dirty_before_commit) {
			zassert_false(
				fp_enc_buffer.is_zeroed(),
				"fp_enc_buffer should not be clean before commit.");
		}
	}

	return EC_RES_SUCCESS;
}

int decrypt_template_v1()
{
	ec_params_fp_template_v1 params = {
		.offset = 0,
		.size = 0,
		.cmd = FP_TEMPLATE_DECRYPT,
	};
	zassert_ok(ec_cmd_fp_template_v1(
		nullptr, &params, offsetof(ec_params_fp_template_v1, data)));

	params.cmd = FP_TEMPLATE_GET_RESULT;
	int status;
	while ((status = ec_cmd_fp_template_v1(
			nullptr, &params,
			offsetof(ec_params_fp_template_v1, data))) ==
	       EC_RES_BUSY) {
		k_msleep(1);
	}
	return status;
}

void enroll_one_template()
{
	zassert_true(
		set_fp_mode(FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE) &
		(FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	update_sim_state([](auto &s) {
		s.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	});

	enroll_percent = 100;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;
	mock_alg_enroll_finish_fake.custom_fake =
		[](const fingerprint_algorithm *const alg, void *templ) -> int {
		std::ranges::copy(kExampleTemplate,
				  static_cast<uint8_t *>(templ));
		return 0;
	};

	fingerprint_run_callback(fp_sim);
	k_msleep(1);
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
	RESET_FAKE(system_is_locked);
	reset_mock_alg_fakes();

	reset_test_fp_context();

	/* Reset seed for random() */
	srandom(0xdeadc0de);

	/* Reset mock controller and template encryption rate-limit deadline. */
	mock_ctrl_fpsensor_crypto = MOCK_CTRL_DEFAULT_FPSENSOR_CRYPTO;
	global_context.templates.encryption_deadline = {};
}

} /* namespace */

ZTEST_USER(fpsensor_template, test_fp_template_v1_load_template_success)
{
	prepare_encrypted_template();
	load_template_v1_chunks();
	zassert_ok(decrypt_template_v1());

	/* Confirm that there is 1 valid template. */
	zassert_ok(get_fp_info_v3(test_info_buffer));
	zassert_equal(test_info_buffer.header.template_info.template_valid, 1);
}

ZTEST_USER(fpsensor_template, test_fp_template_v1_load_template_invalid_tag)
{
	ec_fp_template_encryption_metadata enc_info_with_invalid_tag =
		kExpectedEncInfo;

	/* Corrupt the tag. We expect that the template will be rejected. */
	enc_info_with_invalid_tag.tag[0] = kExpectedEncInfo.tag[0] ^ 0xFF;

	prepare_encrypted_template(enc_info_with_invalid_tag);
	load_template_v1_chunks();

	/* Expect decryption failure (EC_RES_UNAVAILABLE). */
	zassert_equal(EC_RES_UNAVAILABLE, decrypt_template_v1());

	/* Confirm that there is no valid template. */
	zassert_ok(get_fp_info_v3(test_info_buffer));
	zassert_equal(test_info_buffer.header.template_info.template_valid, 0);
}

ZTEST_USER(fpsensor_template, test_fp_template_v1_busy)
{
	ec_params_fp_template_v1 params = {
		.cmd = FP_TEMPLATE_LOAD,
	};

	/* Simulate crypto operation in progress. */
	global_context.sensor_mode |= FP_MODE_ENCRYPT_TEMPLATE;

	zassert_equal(EC_RES_BUSY,
		      ec_cmd_fp_template_v1(nullptr, &params, sizeof(params)));

	params.cmd = FP_TEMPLATE_DECRYPT;
	zassert_equal(EC_RES_BUSY,
		      ec_cmd_fp_template_v1(nullptr, &params, sizeof(params)));

	global_context.sensor_mode &= ~FP_MODE_ENCRYPT_TEMPLATE;
}

ZTEST_USER(fpsensor_template, test_fp_template_v0_busy)
{
	ec_params_fp_template_v1 params_v1 = {
		.cmd = FP_TEMPLATE_DECRYPT,
	};
	ec_params_fp_template params_v0 = {
		.offset = 0,
		.size = 0,
	};

	/* Start decryption via v1 command. */
	zassert_ok(
		ec_cmd_fp_template_v1(nullptr, &params_v1, sizeof(params_v1)));

	/* v0 command should return BUSY. */
	zassert_equal(EC_RES_BUSY, ec_cmd_fp_template(nullptr, &params_v0,
						      sizeof(params_v0)));

	/* Give opportunity for fpsensor task to finish. */
	k_msleep(1);
}

ZTEST_USER(fpsensor_template, test_fp_frame_v0_busy)
{
	ec_params_fp_frame_v1 params_v1 = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};
	ec_params_fp_frame params_v0 = {
		.offset =
			FpFrameV0Offset{
				.buffer_index = FP_FRAME_INDEX_TEMPLATE,
			}
				.pack(),
		.size = 0,
	};

	/* We need at least one valid template for FP_FRAME_ENCRYPT_TEMPLATE. */
	global_context.templates.templ_valid = 1;

	/* Start encryption via v1 command. */
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &params_v1, nullptr));

	/* v0 command should return BUSY. */
	zassert_equal(EC_RES_BUSY, ec_cmd_fp_frame(nullptr, &params_v0,
						   frame_buffer.data()));

	/* Give opportunity for fpsensor task to finish. */
	k_msleep(1);
}

ZTEST_USER(fpsensor_template, test_fp_frame_v0_raw_image_not_busy)
{
	ec_params_fp_template_v1 params_v1 = {
		.cmd = FP_TEMPLATE_DECRYPT,
	};
	ec_params_fp_frame params_v0 = {
		.offset =
			FpFrameV0Offset{
				.buffer_index = FP_FRAME_INDEX_RAW_IMAGE,
			}
				.pack(),
		.size = 0,
	};

	/* Start decryption via v1 command. */
	zassert_ok(
		ec_cmd_fp_template_v1(nullptr, &params_v1, sizeof(params_v1)));

	/*
	 * Confirm that getting raw image is NOT blocked by crypto operation.
	 *
	 * Note: It will return EC_RES_INVALID_PARAM because no image was
	 * captured, but it must NOT be EC_RES_BUSY.
	 */
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_frame(nullptr, &params_v0,
				      frame_buffer.data()));

	/* Give opportunity for fpsensor task to finish. */
	k_msleep(1);
}

ZTEST_USER(fpsensor_template, test_fp_template_v1_overflow)
{
	ec_params_fp_template_v1 params = {
		.cmd = FP_TEMPLATE_LOAD,
	};

	/* Simulate maximum number of templates reached. */
	global_context.templates.templ_valid = FP_MAX_FINGER_COUNT;

	zassert_equal(EC_RES_OVERFLOW,
		      ec_cmd_fp_template_v1(nullptr, &params, sizeof(params)));

	global_context.templates.templ_valid = 0;
}

ZTEST_USER(fpsensor_template, test_fp_template_v1_invalid_params)
{
	FpTemplatePacket<ec_params_fp_template_v1, kTemplateV1ParamsBufferSize>
		params;

	params.header()->cmd = FP_TEMPLATE_LOAD;
	params.header()->offset = 0;
	params.header()->size = 8;

	/* 1. Incorrect args->params_size. */
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_template_v1(
			      nullptr, params.header(),
			      params.packet_size(params.header()->size) + 1));

	/* 2. Offset/size out of bounds. */
	params.header()->offset = FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE;
	params.header()->size = 1;
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_template_v1(
			      nullptr, params.header(),
			      params.packet_size(params.header()->size)));
}

ZTEST_USER(fpsensor_template, test_fp_template_v1_sequential_integrity)
{
	ec_params_fp_template_v1 params = {
		.offset = 0,
		.size = 0,
		.cmd = FP_TEMPLATE_LOAD,
	};

	/* Set FP_ENCRYPTED_TEMPLATE_READY and valid encrypted id. */
	global_context.crypto.encrypted_template_ready = true;
	global_context.templates.template_encrypted_id =
		*FpTemplateId::create(0);

	/* Loading any template chunk should clear them. */
	zassert_ok(ec_cmd_fp_template_v1(nullptr, &params, sizeof(params)));

	zassert_false(global_context.crypto.encrypted_template_ready);
	zassert_false(
		global_context.templates.template_encrypted_id.has_value());
}

ZTEST_USER(fpsensor_template, test_fp_frame_raw_image_system_is_locked)
{
	ec_params_fp_frame frame_request = {
		.offset =
			FpFrameV0Offset{
				.buffer_index = FP_FRAME_INDEX_RAW_IMAGE,
			}
				.pack(),
		.size = IMAGE_SIZE,
	};

	/* Lock the system. */
	system_is_locked_fake.return_val = true;

	/*
	 * Confirm that it's not possible to get raw image when system is
	 * locked.
	 */
	zassert_equal(ec_cmd_fp_frame(nullptr, &frame_request,
				      frame_buffer.data()),
		      EC_RES_ACCESS_DENIED);
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_raw_image_system_is_locked)
{
	ec_params_fp_frame_v1 frame_request = {
		.cmd = FP_FRAME_GET_RAW_IMAGE,
		.offset = 0,
		.size = IMAGE_SIZE,
	};

	/* Lock the system. */
	system_is_locked_fake.return_val = true;

	/*
	 * Confirm that it's not possible to get raw image when system is
	 * locked.
	 */
	zassert_equal(ec_cmd_fp_frame_v1(nullptr, &frame_request,
					 frame_buffer.data()),
		      EC_RES_ACCESS_DENIED);
}

ZTEST_USER(fpsensor_template, test_fp_frame_raw_image_size_too_big)
{
	std::array<uint8_t, IMAGE_SIZE + 1> buffer{};
	ec_params_fp_frame frame_request = {
		.offset =
			FpFrameV0Offset{
				.buffer_index = FP_FRAME_INDEX_RAW_IMAGE,
			}
				.pack(),
		.size = IMAGE_SIZE + 1,
	};

	/*
	 * Confirm that FP_FRAME host command will return an error when
	 * requested more than fingerprint frame size.
	 */
	zassert_equal(ec_cmd_fp_frame(nullptr, &frame_request, buffer.data()),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER(fpsensor_template, test_fp_frame_raw_image_bad_offset)
{
	ec_params_fp_frame frame_request = {
		.offset =
			FpFrameV0Offset{
				.buffer_index = FP_FRAME_INDEX_RAW_IMAGE,
				.offset = IMAGE_SIZE + 1,
			}
				.pack(),
		.size = 1,
	};

	/*
	 * Confirm that FP_FRAME host command will return an error when
	 * trying to read from bad offset.
	 */
	zassert_equal(ec_cmd_fp_frame(nullptr, &frame_request,
				      frame_buffer.data()),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER(fpsensor_template, test_fp_frame_no_template)
{
	ec_params_fp_frame template_request = {
		.offset =
			FpFrameV0Offset{
				.buffer_index = FP_FRAME_INDEX_TEMPLATE,
			}
				.pack(),
		.size = sizeof(encrypted_template),
	};

	/* Get encrypted template (should fail with EC_RES_UNAVAILABLE). */
	zassert_equal(EC_RES_UNAVAILABLE,
		      ec_cmd_fp_frame(nullptr, &template_request,
				      encrypted_template.as_bytes().data()));
}

ZTEST_USER(fpsensor_template, test_fp_frame_template_id_out_of_range)
{
	ec_params_fp_frame template_request = {
		.offset =
			FpFrameV0Offset{
				.buffer_index = FP_FRAME_INDEX_TEMPLATE +
						FP_MAX_FINGER_COUNT,
			}
				.pack(),
		.size = sizeof(encrypted_template),
	};

	/* Get encrypted template (should fail with EC_RES_INVALID_PARAM). */
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_frame(nullptr, &template_request,
				      encrypted_template.as_bytes().data()));
}

ZTEST_USER(fpsensor_template, test_fp_frame_get_encrypted_template_success)
{
	ec_params_fp_frame template_request = {
		.offset =
			FpFrameV0Offset{
				.buffer_index = FP_FRAME_INDEX_TEMPLATE,
			}
				.pack(),
		.size = sizeof(encrypted_template),
	};
	ec_params_fp_read_match_secret secret_params;
	ec_response_fp_read_match_secret secret_response;

	enroll_one_template();

	/* Get encrypted template. */
	zassert_ok(ec_cmd_fp_frame(nullptr, &template_request,
				   encrypted_template.as_bytes().data()));

	check_encrypted_template(encrypted_template);

	/* Read positive match secret for matched template. */
	secret_params.fgr = 0;
	zassert_ok(ec_cmd_fp_read_match_secret(nullptr, &secret_params,
					       &secret_response));
	zassert_true(std::ranges::equal(secret_response.positive_match_secret,
					kExamplePositiveMatchSecret));
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_no_template)
{
	ec_params_fp_frame_v1 template_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};

	/* Encrypt template (should fail with EC_RES_UNAVAILABLE). */
	zassert_equal(EC_RES_UNAVAILABLE,
		      ec_cmd_fp_frame_v1(nullptr, &template_request, nullptr));
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_template_id_out_of_range)
{
	ec_params_fp_frame_v1 template_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = FP_MAX_FINGER_COUNT,
	};

	/* Encrypt template (should fail with EC_RES_INVALID_PARAM). */
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_frame_v1(nullptr, &template_request, nullptr));
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_get_encrypted_template_success)
{
	ec_params_fp_frame_v1 encrypt_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};
	ec_params_fp_frame_v1 get_request = {
		.cmd = FP_FRAME_GET_ENCRYPTED_TEMPLATE,
		.offset = 0,
		.size = sizeof(encrypted_template),
	};

	enroll_one_template();

	/* Request encryption. */
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &encrypt_request, nullptr));

	/* Give opportunity for deferred task to run. */
	k_msleep(1);

	/* Get encrypted template. */
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &get_request,
				      encrypted_template.as_bytes().data()));

	check_encrypted_template(encrypted_template);
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_get_encrypted_template_busy)
{
	ec_params_fp_frame_v1 encrypt_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};
	ec_params_fp_frame_v1 get_request = {
		.cmd = FP_FRAME_GET_ENCRYPTED_TEMPLATE,
		.offset = 0,
		.size = sizeof(encrypted_template),
	};

	enroll_one_template();

	/* Request encryption. */
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &encrypt_request, nullptr));

	/*
	 * Get encrypted template immediately (deferred task didn't run yet).
	 * Should return EC_RES_BUSY.
	 */
	zassert_equal(EC_RES_BUSY,
		      ec_cmd_fp_frame_v1(nullptr, &get_request,
					 encrypted_template.as_bytes().data()));

	/* Give opportunity for deferred task to finish. */
	k_msleep(1);
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_get_encrypted_template_not_ready)
{
	ec_params_fp_frame_v1 get_request = {
		.cmd = FP_FRAME_GET_ENCRYPTED_TEMPLATE,
		.offset = 0,
		.size = sizeof(encrypted_template),
	};

	zassert_equal(EC_RES_UNAVAILABLE,
		      ec_cmd_fp_frame_v1(nullptr, &get_request,
					 encrypted_template.as_bytes().data()));
}

ZTEST_USER(fpsensor_template,
	   test_fp_frame_v1_get_encrypted_template_bad_offset)
{
	ec_params_fp_frame_v1 encrypt_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};
	ec_params_fp_frame_v1 get_request = {
		.cmd = FP_FRAME_GET_ENCRYPTED_TEMPLATE,
		.offset = 0,
		.size = sizeof(encrypted_template) + 1,
	};

	enroll_one_template();

	/* Request encryption. */
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &encrypt_request, nullptr));

	/* Give opportunity for deferred task to run. */
	k_msleep(1);

	/* Get encrypted template with bad size. */
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_frame_v1(nullptr, &get_request,
					 encrypted_template.as_bytes().data()));
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_encrypt_template_busy)
{
	ec_params_fp_frame_v1 encrypt_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};

	enroll_one_template();

	/* Request encryption. */
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &encrypt_request, nullptr));

	/* Request encryption again. Should return EC_RES_BUSY. */
	zassert_equal(EC_RES_BUSY,
		      ec_cmd_fp_frame_v1(nullptr, &encrypt_request, nullptr));

	/* Give opportunity for deferred task to finish. */
	k_msleep(1);
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_encrypt_template_deadline)
{
	ec_params_fp_frame_v1 encrypt_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};

	enroll_one_template();

	/* Request encryption. */
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &encrypt_request, nullptr));

	/* Give opportunity for deferred task to run (clear IN_PROGRESS). */
	k_msleep(100);

	/* Request encryption again (hit deadline). */
	zassert_equal(EC_RES_BUSY,
		      ec_cmd_fp_frame_v1(nullptr, &encrypt_request, nullptr));
}

ZTEST_USER(fpsensor_template,
	   test_fp_frame_v1_encryption_status_ready_set_and_cleared)
{
	ec_params_fp_frame_v1 encrypt_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};
	ec_response_fp_encryption_status status_response;

	enroll_one_template();

	/* Verify initially FP_ENCRYPTED_TEMPLATE_READY is NOT set. */
	zassert_ok(ec_cmd_fp_encryption_status(nullptr, &status_response));
	zassert_false(status_response.status & FP_ENCRYPTED_TEMPLATE_READY);

	/* Request encryption. */
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &encrypt_request, nullptr));

	/* b/114160734: Not more than 1 encrypted message per second. */
	k_sleep(K_SECONDS(1));

	/* Verify FP_ENCRYPTED_TEMPLATE_READY IS set. */
	zassert_ok(ec_cmd_fp_encryption_status(nullptr, &status_response));
	zassert_true(status_response.status & FP_ENCRYPTED_TEMPLATE_READY);

	/* Request encryption AGAIN. */
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &encrypt_request, nullptr));

	/* Verify FP_ENCRYPTED_TEMPLATE_READY IS CLEARED immediately. */
	zassert_ok(ec_cmd_fp_encryption_status(nullptr, &status_response));
	zassert_false(status_response.status & FP_ENCRYPTED_TEMPLATE_READY);

	/* Give opportunity for deferred task to finish. */
	k_msleep(1);

	/* Verify FP_ENCRYPTED_TEMPLATE_READY IS set again. */
	zassert_ok(ec_cmd_fp_encryption_status(nullptr, &status_response));
	zassert_true(status_response.status & FP_ENCRYPTED_TEMPLATE_READY);
}

ZTEST_USER(fpsensor_template, test_fp_template_load_template_success)
{
	prepare_encrypted_template();
	zassert_ok(load_template_v0_chunks());

	/* Confirm that there is 1 valid template. */
	zassert_ok(get_fp_info_v3(test_info_buffer));
	zassert_equal(test_info_buffer.header.template_info.template_valid, 1);
}

ZTEST_USER(fpsensor_template, test_fp_template_load_template_invalid_tag)
{
	ec_fp_template_encryption_metadata enc_info_with_invalid_tag =
		kExpectedEncInfo;

	/* Corrupt the tag. We expect that the template will be rejected. */
	enc_info_with_invalid_tag.tag[0] = 0x00;

	prepare_encrypted_template(enc_info_with_invalid_tag);

	/* Expect decryption failure (EC_RES_UNAVAILABLE). */
	zassert_equal(EC_RES_UNAVAILABLE, load_template_v0_chunks());

	/* Confirm that there is no valid template. */
	zassert_ok(get_fp_info_v3(test_info_buffer));
	zassert_equal(test_info_buffer.header.template_info.template_valid, 0);
}

ZTEST_USER(fpsensor_template, test_fp_template_v1_cleans_buffer)
{
	prepare_encrypted_template();
	load_template_v1_chunks();

	/* Confirm that the buffer is NOT clean before decryption. */
	zassert_false(fp_enc_buffer.is_zeroed(),
		      "fp_enc_buffer should not be clean before decryption.");

	zassert_ok(decrypt_template_v1());

	/* Confirm that the buffer IS clean after decryption. */
	zassert_true(fp_enc_buffer.is_zeroed(),
		     "fp_enc_buffer should be clean after decryption.");
}

ZTEST_USER(fpsensor_template, test_fp_template_v1_cleans_buffer_on_failure)
{
	ec_fp_template_encryption_metadata enc_info_with_invalid_tag =
		kExpectedEncInfo;

	/* Corrupt the tag. We expect that the template will be rejected. */
	enc_info_with_invalid_tag.tag[0] = kExpectedEncInfo.tag[0] ^ 0xFF;

	prepare_encrypted_template(enc_info_with_invalid_tag);
	load_template_v1_chunks();

	/* Confirm that the buffer is NOT clean before decryption. */
	zassert_false(fp_enc_buffer.is_zeroed(),
		      "fp_enc_buffer should not be clean before decryption.");

	(void)decrypt_template_v1();

	/* Confirm that the buffer IS clean even after failed decryption. */
	zassert_true(
		fp_enc_buffer.is_zeroed(),
		"fp_enc_buffer should be clean even after failed decryption.");
}

ZTEST_USER(fpsensor_template, test_fp_template_cleans_buffer)
{
	prepare_encrypted_template();
	zassert_ok(
		load_template_v0_chunks(/*verify_dirty_before_commit=*/true));

	/* Confirm that the buffer IS clean after commit. */
	zassert_true(fp_enc_buffer.is_zeroed(),
		     "fp_enc_buffer should be clean after commit.");
}

ZTEST_USER(fpsensor_template, test_fp_template_cleans_buffer_on_failure)
{
	ec_fp_template_encryption_metadata enc_info_with_invalid_tag =
		kExpectedEncInfo;

	/* Corrupt the tag. We expect that the template will be rejected. */
	enc_info_with_invalid_tag.tag[0] = kExpectedEncInfo.tag[0] ^ 0xFF;

	prepare_encrypted_template(enc_info_with_invalid_tag);

	/* Expect decryption failure (EC_RES_UNAVAILABLE). */
	zassert_equal(
		EC_RES_UNAVAILABLE,
		load_template_v0_chunks(/*verify_dirty_before_commit=*/true));

	/* Confirm that the buffer IS clean even after failed commit. */
	zassert_true(fp_enc_buffer.is_zeroed(),
		     "fp_enc_buffer should be clean even after failed commit.");
}

ZTEST_USER(fpsensor_template,
	   test_fp_frame_v1_encrypt_template_key_derivation_failure)
{
	ec_params_fp_frame_v1 encrypt_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};
	ec_params_fp_frame_v1 get_request = {
		.cmd = FP_FRAME_GET_ENCRYPTED_TEMPLATE,
		.offset = 0,
		.size = sizeof(encrypted_template),
	};

	enroll_one_template();

	/* Inject key derivation failure. */
	mock_ctrl_fpsensor_crypto.output_type =
		MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_FAIL;

	/*
	 * Scheduling the encryption command succeeds (returns EC_RES_SUCCESS),
	 * but the underlying task will fail during key derivation.
	 */
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &encrypt_request, nullptr));

	/* Allow fpsensor task to process the encryption request. */
	k_msleep(1);

	/*
	 * Verify metadata in global fp_enc_buffer remains uninitialized.
	 */
	zassert_equal(
		fp_enc_buffer.metadata.struct_version, 0,
		"struct_version should remain 0 on key derivation failure");
	zassert_true(bytes_are_trivial(fp_enc_buffer.metadata.nonce),
		     "Nonce should remain zeroed on key derivation failure");
	zassert_true(
		bytes_are_trivial(fp_enc_buffer.metadata.encryption_salt),
		"Encryption salt should remain zeroed on key derivation failure");
	zassert_true(bytes_are_trivial(fp_enc_buffer.metadata.tag),
		     "Tag should remain zeroed on key derivation failure");

	/*
	 * Confirm that getting encrypted template returns EC_RES_UNAVAILABLE.
	 */
	zassert_equal(EC_RES_UNAVAILABLE,
		      ec_cmd_fp_frame_v1(nullptr, &get_request,
					 encrypted_template.as_bytes().data()));
}

ZTEST_USER(fpsensor_template, test_encrypt_template_invalid_id)
{
	/* 1. template_encrypted_id == std::nullopt */
	global_context.templates.template_encrypted_id = std::nullopt;
	global_context.sensor_mode = FP_MODE_ENCRYPT_TEMPLATE;
	task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_UPDATE_CONFIG);
	k_msleep(1);
	zassert_false(global_context.crypto.encrypted_template_ready);

	/* 2. template_encrypted_id >= templ_valid (when templ_valid == 0) */
	global_context.templates.templ_valid = 0;
	global_context.templates.template_encrypted_id =
		*FpTemplateId::create(0);
	global_context.sensor_mode = FP_MODE_ENCRYPT_TEMPLATE;
	task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_UPDATE_CONFIG);
	k_msleep(1);
	zassert_false(global_context.crypto.encrypted_template_ready);
}

ZTEST_USER(fpsensor_template, test_fp_frame_v0_and_v1_edge_cases)
{
	ec_params_fp_frame frame_v0 = {
		.offset =
			FpFrameV0Offset{
				.buffer_index = FP_FRAME_INDEX_TEMPLATE,
			}
				.pack(),
		.size = 16,
	};
	ec_params_fp_frame_v1 frame_v1 = {
		.cmd = FP_FRAME_GET_RAW_IMAGE,
		.offset = 0,
		.size = 16,
	};

	/* 1. size > args->output_buf_max for v0 and v1 */
	zassert_equal(EC_RES_INVALID_PARAM,
		      CROS_EC_COMMAND(nullptr, EC_CMD_FP_FRAME, 0, &frame_v0,
				      sizeof(frame_v0),
				      encrypted_template.as_bytes().data(), 8));
	zassert_equal(EC_RES_INVALID_PARAM,
		      CROS_EC_COMMAND(nullptr, EC_CMD_FP_FRAME, 1, &frame_v1,
				      sizeof(frame_v1),
				      encrypted_template.as_bytes().data(), 8));

	/* 2. Unknown command in fp_command_frame_v1 */
	frame_v1.cmd = 99;
	frame_v1.size = 0;
	zassert_ok(ec_cmd_fp_frame_v1(nullptr, &frame_v1,
				      encrypted_template.as_bytes().data()));

	/* 3. Enroll a template to test v0 template read edge cases. */
	enroll_one_template();

	/* 4. v0 template read with invalid offset/size */
	frame_v0.offset =
		FpFrameV0Offset{
			.buffer_index = FP_FRAME_INDEX_TEMPLATE,
			.offset = sizeof(fp_enc_buffer),
		}
			.pack();
	frame_v0.size = 1;
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_frame(nullptr, &frame_v0,
				      encrypted_template.as_bytes().data()));

	/* 5. v0 template read encryption failure at offset == 0 */
	mock_ctrl_fpsensor_crypto.output_type =
		MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_FAIL;
	frame_v0.offset =
		FpFrameV0Offset{
			.buffer_index = FP_FRAME_INDEX_TEMPLATE,
		}
			.pack();
	frame_v0.size = 16;
	zassert_equal(EC_RES_UNAVAILABLE,
		      ec_cmd_fp_frame(nullptr, &frame_v0,
				      encrypted_template.as_bytes().data()));

	/* 6. Immediate retry hits 1-second encryption_deadline (EC_RES_BUSY) */
	mock_ctrl_fpsensor_crypto = MOCK_CTRL_DEFAULT_FPSENSOR_CRYPTO;
	zassert_equal(EC_RES_BUSY,
		      ec_cmd_fp_frame(nullptr, &frame_v0,
				      encrypted_template.as_bytes().data()));

	/* 7. Non-zero offset chunk read succeeds without re-encrypting */
	frame_v0.offset =
		FpFrameV0Offset{
			.buffer_index = FP_FRAME_INDEX_TEMPLATE,
			.offset = 16,
		}
			.pack();
	frame_v0.size = 16;
	zassert_ok(ec_cmd_fp_frame(nullptr, &frame_v0,
				   encrypted_template.as_bytes().data()));
}

ZTEST_USER(fpsensor_template, test_fp_template_v0_and_v1_edge_cases)
{
	FpTemplatePacket<ec_params_fp_template, kTemplateParamsBufferSize>
		params_v0;
	ec_params_fp_template_v1 params_v1 = {};

	/* 1. v0 overflow when templ_valid >= FP_MAX_FINGER_COUNT */
	global_context.templates.templ_valid = FP_MAX_FINGER_COUNT;
	zassert_equal(EC_RES_OVERFLOW,
		      ec_cmd_fp_template(nullptr, params_v0.header(),
					 sizeof(*params_v0.header())));
	global_context.templates.templ_valid = 0;

	/* 2. v0 params_size mismatch */
	params_v0.header()->offset = 0;
	params_v0.header()->size = 4;
	zassert_equal(
		EC_RES_INVALID_PARAM,
		ec_cmd_fp_template(
			nullptr, params_v0.header(),
			params_v0.packet_size(params_v0.header()->size) + 1));

	/* 3. v0 validate_fp_buffer_offset failure */
	params_v0.header()->offset = sizeof(fp_enc_buffer);
	params_v0.header()->size = 1;
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_template(
			      nullptr, params_v0.header(),
			      params_v0.packet_size(params_v0.header()->size)));

	/* 4. v1 invalid command */
	params_v1.cmd = 99;
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_template_v1(nullptr, &params_v1,
					    sizeof(params_v1)));

	/* 5. fp_commit_template overflow when templ_valid reaches max */
	global_context.templates.templ_valid = FP_MAX_FINGER_COUNT;
	global_context.sensor_mode = FP_MODE_DECRYPT_TEMPLATE;
	task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_UPDATE_CONFIG);
	k_msleep(1);
	params_v1.cmd = FP_TEMPLATE_GET_RESULT;
	zassert_equal(EC_RES_OVERFLOW,
		      ec_cmd_fp_template_v1(nullptr, &params_v1,
					    sizeof(params_v1)));
	global_context.templates.templ_valid = 0;
}

ZTEST_SUITE(fpsensor_template, nullptr, fpsensor_setup, fpsensor_before,
	    nullptr, nullptr);
