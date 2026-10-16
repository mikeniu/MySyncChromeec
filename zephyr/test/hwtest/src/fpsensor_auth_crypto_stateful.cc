/* Copyright 2024 The ChromiumOS Authors
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

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <algorithm>
#include <array>

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

constexpr std::array<uint8_t, 32> kFakeData = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
						1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1,
						2, 3, 4, 5, 6, 7, 8, 9, 1, 2 };
static_assert(kFakeData.size() == FP_PAIRING_KEY_LEN);

void *setup_suite(void)
{
	if (IS_ENABLED(CONFIG_OTP_KEY)) {
		std::ranges::copy(default_fake_otp_key,
				  mock_otp.otp_key_buffer);
	}

	return nullptr;
}

ZTEST_SUITE(fpsensor_auth_crypto_stateful, nullptr, setup_suite, nullptr,
	    nullptr, nullptr);

ZTEST(fpsensor_auth_crypto_stateful, test_fp_encrypt_fail_size_mismatch)
{
	std::array<uint8_t, 31> too_small_buffer{};
	zassert_equal(encrypt_data(1, kFakeUserId, kFakeTpmSeed, kFakeData,
				   too_small_buffer)
			      .error(),
		      EC_ERROR_OVERFLOW);

	std::array<uint8_t, 33> too_big_buffer{};
	zassert_equal(encrypt_data(1, kFakeUserId, kFakeTpmSeed, kFakeData,
				   too_big_buffer)
			      .error(),
		      EC_ERROR_OVERFLOW);
}

ZTEST(fpsensor_auth_crypto_stateful, test_fp_decrypt_fail_size_mismatch)
{
	uint16_t version = 1;
	std::array<uint8_t, 32> enc_data{};

	const auto info = encrypt_data(version, kFakeUserId, kFakeTpmSeed,
				       kFakeData, enc_data);
	zassert_true(info.has_value());

	std::array<uint8_t, 31> too_small_buffer{};
	zassert_equal(decrypt_data(*info, kFakeUserId, kFakeTpmSeed, enc_data,
				   too_small_buffer)
			      .error(),
		      EC_ERROR_OVERFLOW);

	std::array<uint8_t, 33> too_big_buffer{};
	zassert_equal(decrypt_data(*info, kFakeUserId, kFakeTpmSeed, enc_data,
				   too_big_buffer)
			      .error(),
		      EC_ERROR_OVERFLOW);
}

ZTEST(fpsensor_auth_crypto_stateful, test_fp_encrypt_decrypt_data)
{
	uint16_t version = 1;
	std::array<uint8_t, 32> enc_data{};

	const auto info = encrypt_data(version, kFakeUserId, kFakeTpmSeed,
				       kFakeData, enc_data);
	zassert_true(info.has_value());

	zassert_equal(info->struct_version, version);

	/* The encrypted data should not be the same as the input. */
	zassert_false(enc_data == kFakeData);

	std::array<uint8_t, 32> output{};
	zassert_true(
		decrypt_data(*info, kFakeUserId, kFakeTpmSeed, enc_data, output)
			.has_value());

	zassert_true(output == kFakeData);
}

ZTEST(fpsensor_auth_crypto_stateful, test_fp_encrypt_decrypt_data_in_place)
{
	uint16_t version = 1;
	std::array<uint8_t, 32> data = kFakeData;

	const auto info =
		encrypt_data_in_place(version, kFakeUserId, kFakeTpmSeed, data);
	zassert_true(info.has_value());

	zassert_equal(info->struct_version, version);

	/* The encrypted data should not be the same as the input. */
	zassert_false(data == kFakeData);

	std::array<uint8_t, 32> output{};
	zassert_true(
		decrypt_data(*info, kFakeUserId, kFakeTpmSeed, data, output)
			.has_value());

	zassert_true(output == kFakeData);
}

ZTEST(fpsensor_auth_crypto_stateful, test_fp_encrypt_decrypt_pairing_key)
{
	uint16_t version = 1;

	const auto encrypted = encrypt_pairing_key(version, kFakeData);
	zassert_true(encrypted.has_value());

	zassert_equal(encrypted->info.struct_version, version);

	/* The encrypted data should not be the same as the input. */
	zassert_false(std::ranges::equal(encrypted->data, kFakeData));

	const auto output =
		decrypt_pairing_key(encrypted->info, encrypted->data);
	zassert_true(output.has_value());

	zassert_true(*output == kFakeData);
}
