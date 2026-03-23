/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "file_control.h"
#include "iap_control.h"
#include "utility.h"

namespace elan {
namespace {

// 1. Test utility.h (Error string mapping)
TEST(UtilityTest, ToStringMapping) {
  EXPECT_EQ(ToString(IapError::FileOpenFailed), "Failed to open file");
  EXPECT_EQ(ToString(IapError::InvalidFirmware),
            "Firmware prefix does not match Google requirement");
}

// 2. Test iap_control.h (Packet Structures & Spans)
TEST(IapPacketTest, CommandPacketSpan) {
  IapCommandPacket pkt{
      .cmd_code = 0x20, .reserved = {0, 0, 0}, .payload_le = 0x12345678};
  auto span = pkt.AsByteSpan();
  ASSERT_EQ(span.size(), 8);
  EXPECT_EQ(span[0], 0x20);
}

TEST(IapPacketTest, StatusPacketSpan) {
  IapStatusPacket pkt{.cmd_echo = 0x20,
                      .status_code = 0x01,
                      .boot_type = 0x02,
                      .reserved = 0,
                      .return_value = 0xAABBCCDD};
  auto span = pkt.AsByteSpan();
  ASSERT_EQ(span.size(), 8);
  EXPECT_EQ(span[0], 0x20);
  EXPECT_EQ(span[1], 0x01);
}

// 3. Test file_control.h (File I/O Operations)
TEST(FileControlTest, GetBinaryFileNotFound) {
  auto res = GetBinary("non_existent_file_xyz123.bin");
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), IapError::FileOpenFailed);
}

TEST(FileControlTest, GetBinarySuccess) {
  const std::string temp_filename = "temp_test_fw.bin";
  const std::vector<uint8_t> expected_data = {0x10, 0x20, 0x30, 0x40, 0x50};

  // Create temporary file
  {
    std::ofstream ofs(temp_filename, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(ofs.is_open());
    ofs.write(reinterpret_cast<const char *>(expected_data.data()),
              expected_data.size());
  }

  // Read binary back using vendor's GetBinary
  auto res = GetBinary(temp_filename);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res.value(), expected_data);

  // Clean up
  std::remove(temp_filename.c_str());
}

}  // namespace
}  // namespace elan

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
