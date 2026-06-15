/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crypto_util.h"

#include <array>
#include <ranges>
#include <vector>

#include <gtest/gtest.h>
#include <openssl/sha.h>

namespace egis {
namespace {

constexpr auto kTestKey = std::to_array<uint8_t>(
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
     0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
     0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f});

constexpr auto kTestPlaintext =
    std::to_array<uint8_t>({'C', 'h', 'r', 'o', 'm', 'i', 'u', 'm', 'O', 'S'});

constexpr auto kTestAad = std::to_array<uint8_t>({0xAA, 0xBB, 0xCC, 0xDD});

TEST(CryptoUtilTest, Sha256KnownValue) {
  const auto data = std::to_array<uint8_t>({'a', 'b', 'c'});

  // SHA256("abc") ->
  // ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
  const auto kExpectedHash = std::to_array<uint8_t>(
      {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
       0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
       0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad});

  auto hash = crypto::Sha256(data);

  EXPECT_EQ(kExpectedHash, hash);
}

TEST(CryptoUtilTest, GenerateGcmIvIsUniqueAndNonZero) {
  auto iv1_res = crypto::GenerateGcmIv();
  auto iv2_res = crypto::GenerateGcmIv();

  ASSERT_TRUE(iv1_res.has_value());
  ASSERT_TRUE(iv2_res.has_value());

  auto iv1 = *iv1_res;
  auto iv2 = *iv2_res;

  // It is cryptographically improbable (1 in 2^96) for two generated IVs to
  // match, or for an IV to be entirely zeroes if the RNG is working correctly.
  std::array<uint8_t, crypto::kAesGcmIvSize> all_zeros{};
  EXPECT_NE(iv1, all_zeros);
  EXPECT_NE(iv1, iv2);
}

TEST(CryptoUtilTest, AesGcmRoundTripWithAad) {
  auto iv_res = crypto::GenerateGcmIv();
  ASSERT_TRUE(iv_res.has_value());
  auto iv = *iv_res;

  std::vector<uint8_t> ciphertext(kTestPlaintext.size());
  std::array<uint8_t, crypto::kAesGcmTagSize> tag{};

  auto enc_res = crypto::AesGcmEncrypt(kTestKey, iv, kTestAad, kTestPlaintext,
                                       ciphertext, tag);
  ASSERT_TRUE(enc_res.has_value());

  std::array<uint8_t, kTestPlaintext.size()> decrypted{};
  auto dec_res =
      crypto::AesGcmDecrypt(kTestKey, iv, kTestAad, ciphertext, tag, decrypted);
  ASSERT_TRUE(dec_res.has_value());

  EXPECT_EQ(kTestPlaintext, decrypted);
}

TEST(CryptoUtilTest, AesGcmRoundTripWithoutAad) {
  auto iv_res = crypto::GenerateGcmIv();
  ASSERT_TRUE(iv_res.has_value());
  auto iv = *iv_res;

  std::vector<uint8_t> ciphertext(kTestPlaintext.size());
  std::array<uint8_t, crypto::kAesGcmTagSize> tag{};

  ASSERT_TRUE(
      crypto::AesGcmEncrypt(kTestKey, iv, {}, kTestPlaintext, ciphertext, tag)
          .has_value());

  std::array<uint8_t, kTestPlaintext.size()> decrypted{};
  ASSERT_TRUE(
      crypto::AesGcmDecrypt(kTestKey, iv, {}, ciphertext, tag, decrypted)
          .has_value());

  EXPECT_EQ(kTestPlaintext, decrypted);
}

TEST(CryptoUtilTest, AesGcmFailsOnTagTampering) {
  auto iv_res = crypto::GenerateGcmIv();
  ASSERT_TRUE(iv_res.has_value());
  auto iv = *iv_res;

  std::vector<uint8_t> ciphertext(kTestPlaintext.size());
  std::array<uint8_t, crypto::kAesGcmTagSize> tag{};

  ASSERT_TRUE(crypto::AesGcmEncrypt(kTestKey, iv, kTestAad, kTestPlaintext,
                                    ciphertext, tag)
                  .has_value());

  tag[0] ^= 0xFF;

  std::array<uint8_t, kTestPlaintext.size()> decrypted{};
  auto dec_res =
      crypto::AesGcmDecrypt(kTestKey, iv, kTestAad, ciphertext, tag, decrypted);

  ASSERT_FALSE(dec_res.has_value());
  EXPECT_EQ(dec_res.error(), crypto::CryptoError::kTagMismatch);
}

TEST(CryptoUtilTest, AesGcmFailsOnCiphertextTampering) {
  auto iv_res = crypto::GenerateGcmIv();
  ASSERT_TRUE(iv_res.has_value());
  auto iv = *iv_res;

  std::vector<uint8_t> ciphertext(kTestPlaintext.size());
  std::array<uint8_t, crypto::kAesGcmTagSize> tag{};

  ASSERT_TRUE(crypto::AesGcmEncrypt(kTestKey, iv, kTestAad, kTestPlaintext,
                                    ciphertext, tag)
                  .has_value());

  ciphertext[0] ^= 0xFF;

  std::array<uint8_t, kTestPlaintext.size()> decrypted{};
  auto dec_res =
      crypto::AesGcmDecrypt(kTestKey, iv, kTestAad, ciphertext, tag, decrypted);

  ASSERT_FALSE(dec_res.has_value());
  EXPECT_EQ(dec_res.error(), crypto::CryptoError::kTagMismatch);
}

TEST(CryptoUtilTest, AesGcmFailsOnAadTampering) {
  auto iv_res = crypto::GenerateGcmIv();
  ASSERT_TRUE(iv_res.has_value());
  auto iv = *iv_res;

  std::vector<uint8_t> ciphertext(kTestPlaintext.size());
  std::array<uint8_t, crypto::kAesGcmTagSize> tag{};

  ASSERT_TRUE(crypto::AesGcmEncrypt(kTestKey, iv, kTestAad, kTestPlaintext,
                                    ciphertext, tag)
                  .has_value());

  auto bad_aad = std::to_array<uint8_t>({0xDE, 0xAD, 0xBE, 0xEF});

  std::array<uint8_t, kTestPlaintext.size()> decrypted{};
  auto dec_res =
      crypto::AesGcmDecrypt(kTestKey, iv, bad_aad, ciphertext, tag, decrypted);

  ASSERT_FALSE(dec_res.has_value());
  EXPECT_EQ(dec_res.error(), crypto::CryptoError::kTagMismatch);
}

TEST(CryptoUtilTest, CleanseZeroesBuffer) {
  auto sensitive_data = std::to_array<uint8_t>({0xDE, 0xAD, 0xBE, 0xEF});
  const auto expected_zeros = std::to_array<uint8_t>({0x00, 0x00, 0x00, 0x00});

  crypto::Cleanse(sensitive_data);

  EXPECT_EQ(expected_zeros, sensitive_data);
}
}  // namespace
}  // namespace egis
