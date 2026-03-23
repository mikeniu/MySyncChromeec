// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utility.h"

#include <gtest/gtest.h>

#include <vector>

namespace elan {
namespace {

TEST(UtilityTest, ToStringMapping) {
  EXPECT_EQ(ToString(IapError::FileOpenFailed), "Failed to open file");
  EXPECT_EQ(ToString(IapError::InvalidFirmware),
            "Firmware prefix does not match Google requirement");
}

TEST(UtilityTest, ToLittleEndianConvertsCorrectly) {
  uint32_t val = 0x11223344;
  // Since x86/ARM natively use Little Endian, this ensures the byteswap
  // template doesn't accidentally corrupt data on standard host machines.
  EXPECT_EQ(ToLittleEndian(val), 0x11223344);
}

TEST(UtilityTest, Get32LeRejectsSmallBuffer) {
  std::vector<uint8_t> small_buffer = {0x01, 0x02, 0x03};  // 3 bytes

  auto result = Get32Le(small_buffer);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), IapError::BufferTooSmall);
}

TEST(UtilityTest, Set32LeRejectsSmallBuffer) {
  std::vector<uint8_t> small_buffer(3, 0);  // 3 bytes

  auto result = Set32Le(small_buffer, 0xFFFFFFFF);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), IapError::BufferTooSmall);
}

TEST(UtilityTest, GetAndSet32LeRoundTrip) {
  std::vector<uint8_t> buffer(4, 0);
  uint32_t expected_val = 0xDEADBEEF;

  ASSERT_TRUE(Set32Le(buffer, expected_val).has_value());

  auto result = Get32Le(buffer);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), expected_val);
}

}  // namespace
}  // namespace elan
