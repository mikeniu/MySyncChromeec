/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "crypto_util.h"

#include <gtest/gtest.h>

#include <array>

namespace egis {
namespace {
TEST(CryptoUtilTest, Sha256KnownValue) {
  const uint8_t data[] = {'a', 'b', 'c'};
  std::array<uint8_t, 32> hash{};
  crypto::Sha256(data, hash);
  EXPECT_EQ(hash[0], 0xba);
  EXPECT_EQ(hash[1], 0x78);
  EXPECT_EQ(hash[2], 0x16);
  EXPECT_EQ(hash[3], 0xbf);
}
}  // namespace
}  // namespace egis
