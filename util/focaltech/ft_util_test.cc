/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ft_util.h"

#include <gtest/gtest.h>

namespace focaltech {

// -----------------------------------------------------------------------------
// DivRoundUp Tests
// -----------------------------------------------------------------------------
TEST(FtUtilTest, DivRoundUp_ExactDivision) {
  EXPECT_EQ(DivRoundUp(10, 5), 2);
  EXPECT_EQ(DivRoundUp(100, 10), 10);
  EXPECT_EQ(DivRoundUp(0, 5), 0);
}

TEST(FtUtilTest, DivRoundUp_NeedRoundUp) {
  EXPECT_EQ(DivRoundUp(10, 3), 4);  // 10/3 = 3.33 -> 4
  EXPECT_EQ(DivRoundUp(11, 3), 4);  // 11/3 = 3.66 -> 4
  EXPECT_EQ(DivRoundUp(12, 5), 3);  // 12/5 = 2.4 -> 3
  EXPECT_EQ(DivRoundUp(1, 2), 1);   // 0.5 -> 1
}

TEST(FtUtilTest, DivRoundUp_LargeNumbers) {
  constexpr size_t kLarge = 1000000;
  EXPECT_EQ(DivRoundUp(kLarge, 1uz), kLarge);
  EXPECT_EQ(DivRoundUp(kLarge, kLarge), 1);
  EXPECT_EQ(DivRoundUp(kLarge + 1, kLarge), 2);
}

TEST(FtUtilTest, DivRoundUp_DifferentTypes) {
  EXPECT_EQ(DivRoundUp(10u, 3u), 4u);
  EXPECT_EQ(DivRoundUp(10L, 3L), 4L);
  EXPECT_EQ(DivRoundUp<int>(10, 3), 4);
  EXPECT_EQ(DivRoundUp<size_t>(10, 3), 4);
}

// -----------------------------------------------------------------------------
// Error ToString Tests
// -----------------------------------------------------------------------------
TEST(FtUtilTest, ToString_ReturnsCorrectString) {
  EXPECT_EQ(ToString(Error::kSuccess), "Success");
  EXPECT_EQ(ToString(Error::kDeviceNotFound), "Device not found");
  EXPECT_EQ(ToString(Error::kInvalidParameter), "Invalid parameter");
  EXPECT_EQ(ToString(Error::kFileNotFound), "File not found");
  EXPECT_EQ(ToString(Error::kHardwareFailure), "Hardware failure");
  EXPECT_EQ(ToString(Error::kInvalidMode), "Invalid mode");
  EXPECT_EQ(ToString(Error::kInvalidFormat), "Invalid firmware format");
  EXPECT_EQ(ToString(Error::kVerificationFailed), "Verification failed");
}

TEST(FtUtilTest, ToString_UnknownError) {
  auto unknown = Error{100};
  EXPECT_EQ(ToString(unknown), "Unknown error");
}

// Compile-time checks for constexpr
static_assert(DivRoundUp(5, 2) == 3, "constexpr test failed");
static_assert(DivRoundUp(8, 4) == 2, "constexpr test failed");

}  // namespace focaltech
