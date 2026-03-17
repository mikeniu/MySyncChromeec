/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "crypto_util.h"

#include <gtest/gtest.h>
#include <openssl/sha.h>

#include <array>

namespace egis {
namespace {
TEST(CryptoUtilTest, Sha256KnownValue) {
  const uint8_t data[] = {'a', 'b', 'c'};
  // SHA256("abc") -> ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c
  // b410ff61 f20015ad
  const std::array<uint8_t, 4> kExpectedHashPrefix = {0xba, 0x78, 0x16, 0xbf};
  std::array<uint8_t, SHA256_DIGEST_LENGTH> hash{};
  crypto::Sha256(data, hash);
  EXPECT_TRUE(std::equal(kExpectedHashPrefix.begin(), kExpectedHashPrefix.end(),
                         hash.begin()));
}
}  // namespace
}  // namespace egis
