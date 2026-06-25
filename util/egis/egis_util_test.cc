/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "egis_util.h"

#include <array>

#include <gtest/gtest.h>

namespace egis {
namespace {
struct SampleStruct {
  uint16_t a;
  uint32_t b;
} __attribute__((packed));

TEST(EgisUtilTest, EndianSwap) {
  uint32_t val = 0x12345678;
  EXPECT_EQ(ToLittleEndian(val), val);
}

TEST(EgisUtilTest, ReadWriteStruct) {
  std::array<uint8_t, 6> buffer{};
  SampleStruct in{.a = 0xAAAA, .b = 0xBBBBCCCC};
  ASSERT_TRUE(WriteStruct<SampleStruct>(buffer, in).has_value());
  auto read_res = ReadStruct<SampleStruct>(buffer);
  ASSERT_TRUE(read_res.has_value());
  EXPECT_EQ(read_res->a, 0xAAAA);
  EXPECT_EQ(read_res->b, 0xBBBBCCCC);
}
}  // namespace
}  // namespace egis
