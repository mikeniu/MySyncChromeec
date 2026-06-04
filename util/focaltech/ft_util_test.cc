/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ft_util.h"

#include <gtest/gtest.h>

// -----------------------------------------------------------------------------
// FtDivRoundUp Tests
// -----------------------------------------------------------------------------
TEST(FtUtilTest, DivRoundUp_ExactDivision) {
  EXPECT_EQ(FtDivRoundUp(10, 5), 2);
  EXPECT_EQ(FtDivRoundUp(100, 10), 10);
  EXPECT_EQ(FtDivRoundUp(0, 5), 0);
}

TEST(FtUtilTest, DivRoundUp_NeedRoundUp) {
  EXPECT_EQ(FtDivRoundUp(10, 3), 4);  // 10/3 = 3.33 -> 4
  EXPECT_EQ(FtDivRoundUp(11, 3), 4);  // 11/3 = 3.66 -> 4
  EXPECT_EQ(FtDivRoundUp(12, 5), 3);  // 12/5 = 2.4 -> 3
  EXPECT_EQ(FtDivRoundUp(1, 2), 1);   // 0.5 -> 1
}

TEST(FtUtilTest, DivRoundUp_LargeNumbers) {
  constexpr size_t kLarge = 1000000;
  EXPECT_EQ(FtDivRoundUp(kLarge, 1uz), kLarge);  // C++23 uz suffix
  EXPECT_EQ(FtDivRoundUp(kLarge, kLarge), 1);
  EXPECT_EQ(FtDivRoundUp(kLarge + 1, kLarge), 2);
}

TEST(FtUtilTest, DivRoundUp_DifferentTypes) {
  // Same type
  EXPECT_EQ(FtDivRoundUp(10u, 3u), 4u);
  EXPECT_EQ(FtDivRoundUp(10L, 3L), 4L);
  // Mixed types: the template deduces common type, but caller should match
  EXPECT_EQ(FtDivRoundUp<int>(10, 3), 4);
  EXPECT_EQ(FtDivRoundUp<size_t>(10, 3), 4);
}

// -----------------------------------------------------------------------------
// FtError ToString Tests
// -----------------------------------------------------------------------------
TEST(FtUtilTest, ToString_ReturnsCorrectString) {
  EXPECT_EQ(ToString(FtError::kSuccess), "Success");
  EXPECT_EQ(ToString(FtError::kNoSuchDevice), "No such device");
  EXPECT_EQ(ToString(FtError::kParameterError), "Parameter error");
  EXPECT_EQ(ToString(FtError::kNoSuchFile), "No such file");
  EXPECT_EQ(ToString(FtError::kResultFail), "Result fail");
  EXPECT_EQ(ToString(FtError::kInvalidMode), "Invalid mode");
}

TEST(FtUtilTest, ToString_UnknownError) {
  auto unknown = FtError{100};
  EXPECT_EQ(ToString(unknown), "Unknown error");
}

// Compile-time checks for constexpr
static_assert(FtDivRoundUp(5, 2) == 3, "constexpr test failed");
static_assert(FtDivRoundUp(8, 4) == 2, "constexpr test failed");
