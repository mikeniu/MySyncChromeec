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

TEST(UtilityTest, EndianConversions) {
  uint32_t val = 0x11223344;
  // Since x86/ARM natively use Little Endian, this ensures the byteswap
  // template doesn't accidentally corrupt data on standard host machines.
  EXPECT_EQ(ToLittleEndian(val), 0x11223344);
  EXPECT_EQ(FromLittleEndian(val), 0x11223344);
}

}  // namespace
}  // namespace elan
