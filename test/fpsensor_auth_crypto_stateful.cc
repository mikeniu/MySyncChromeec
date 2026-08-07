/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "mock/otpi_mock.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/obj_mac.h"
#include "otp_key.h"
#include "test_util.h"
#include "util.h"

#include <assert.h>
#include <stdbool.h>

#include <algorithm>
#include <array>

#if defined(BASEBOARD_HELIPILOT)
BUILD_ASSERT(IS_ENABLED(CONFIG_OTP_KEY),
	     "OTP_KEY should be enabled for Helipilot baseboard.");
#endif

bool fingerprint_auth_enabled(void)
{
	return true;
}

namespace
{

constexpr std::array<uint8_t, 32> kFakeTpmSeed = {
	0xd9, 0x71, 0xaf, 0xc4, 0xcd, 0x36, 0xe3, 0x60, 0xf8, 0x5a, 0xa0,
	0xa6, 0x2c, 0xb3, 0xf5, 0xe2, 0xeb, 0xb9, 0xd8, 0x2f, 0xb5, 0x78,
	0x5c, 0x79, 0x82, 0xce, 0x06, 0x3f, 0xcc, 0x23, 0xb9, 0xe7,
};
static_assert(kFakeTpmSeed.size() == FP_CONTEXT_TPM_BYTES);

constexpr std::array<uint8_t, 32> kFakeUserId = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05,  0x06, 0x07,  0x08, 0x09, 0x0a,
	0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x010, 0x11, 0x012, 0x13, 0x14, 0x15,
	0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,  0x1c, 0x1d,  0x1e, 0x1f
};
static_assert(kFakeUserId.size() == FP_CONTEXT_USERID_BYTES);

test_static enum ec_error_list test_fp_encrypt_fail_size_mismatch(void)
{
	struct fp_auth_command_encryption_metadata info{};
	const std::array<uint8_t, 32> input = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
						1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
						2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };
	std::array<uint8_t, 31> too_small_buffer{};
	TEST_EQ(encrypt_data(1, info, kFakeUserId, kFakeTpmSeed, input,
			     too_small_buffer),
		EC_ERROR_OVERFLOW, "%d");

	std::array<uint8_t, 33> too_big_buffer{};
	TEST_EQ(encrypt_data(1, info, kFakeUserId, kFakeTpmSeed, input,
			     too_big_buffer),
		EC_ERROR_OVERFLOW, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_decrypt_fail_size_mismatch(void)
{
	struct fp_auth_command_encryption_metadata info{};
	const std::array<uint8_t, 32> input = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
						1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
						2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };

	uint16_t version = 1;
	std::array<uint8_t, 32> enc_data{};
	TEST_EQ(encrypt_data(version, info, kFakeUserId, kFakeTpmSeed, input,
			     enc_data),
		EC_SUCCESS, "%d");

	std::array<uint8_t, 31> too_small_buffer{};
	TEST_EQ(decrypt_data(info, kFakeUserId, kFakeTpmSeed, enc_data,
			     too_small_buffer),
		EC_ERROR_OVERFLOW, "%d");

	std::array<uint8_t, 33> too_big_buffer{};
	TEST_EQ(decrypt_data(info, kFakeUserId, kFakeTpmSeed, enc_data,
			     too_big_buffer),
		EC_ERROR_OVERFLOW, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_encrypt_decrypt_data(void)
{
	struct fp_auth_command_encryption_metadata info{};
	const std::array<uint8_t, 32> input = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
						1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
						2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };
	uint16_t version = 1;
	std::array<uint8_t, 32> enc_data{};

	TEST_EQ(encrypt_data(version, info, kFakeUserId, kFakeTpmSeed, input,
			     enc_data),
		EC_SUCCESS, "%d");

	TEST_EQ(info.struct_version, version, "%d");

	/* The encrypted data should not be the same as the input. */
	TEST_ASSERT_ARRAY_NE(enc_data, input, enc_data.size());

	std::array<uint8_t, 32> output{};
	TEST_EQ(decrypt_data(info, kFakeUserId, kFakeTpmSeed, enc_data, output),
		EC_SUCCESS, "%d");

	TEST_ASSERT_ARRAY_EQ(input, output, sizeof(input));

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_encrypt_decrypt_data_in_place(void)
{
	struct fp_auth_command_encryption_metadata info{};
	const std::array<uint8_t, 32> input = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
						1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
						2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };
	uint16_t version = 1;
	std::array<uint8_t, 32> data = input;

	TEST_EQ(encrypt_data_in_place(version, info, kFakeUserId, kFakeTpmSeed,
				      data),
		EC_SUCCESS, "%d");

	TEST_EQ(info.struct_version, version, "%d");

	/* The encrypted data should not be the same as the input. */
	TEST_ASSERT_ARRAY_NE(data, input, data.size());

	std::array<uint8_t, 32> output{};
	TEST_EQ(decrypt_data(info, kFakeUserId, kFakeTpmSeed, data, output),
		EC_SUCCESS, "%d");

	TEST_ASSERT_ARRAY_EQ(input, output, sizeof(input));

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_encrypt_decrypt_pairing_key(void)
{
	struct fp_auth_command_encryption_metadata info{};
	const std::array<uint8_t, FP_PAIRING_KEY_LEN> input = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2
	};
	uint16_t version = 1;
	std::array<uint8_t, FP_PAIRING_KEY_LEN> enc_data{};

	TEST_EQ(encrypt_pairing_key(version, info, input, enc_data), EC_SUCCESS,
		"%d");

	TEST_EQ(info.struct_version, version, "%d");

	/* The encrypted data should not be the same as the input. */
	TEST_ASSERT_ARRAY_NE(enc_data, input, enc_data.size());

	std::array<uint8_t, FP_PAIRING_KEY_LEN> output{};
	TEST_EQ(decrypt_pairing_key(info, enc_data, output), EC_SUCCESS, "%d");

	TEST_ASSERT_ARRAY_EQ(input, output, sizeof(input));

	return EC_SUCCESS;
}

} // namespace

void run_test(int argc, const char **argv)
{
	/*
	 * Set the OTP key to since the following tests require it.
	 */
	if (IS_ENABLED(CONFIG_OTP_KEY)) {
		std::ranges::copy(default_fake_otp_key,
				  mock_otp.otp_key_buffer);
	}
	RUN_TEST(test_fp_encrypt_fail_size_mismatch);
	RUN_TEST(test_fp_decrypt_fail_size_mismatch);
	RUN_TEST(test_fp_encrypt_decrypt_data);
	RUN_TEST(test_fp_encrypt_decrypt_data_in_place);
	RUN_TEST(test_fp_encrypt_decrypt_pairing_key);
	test_print_result();
}
