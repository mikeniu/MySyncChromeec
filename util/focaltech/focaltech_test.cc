/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>

#include "ft_help.h"
#include "ft_scsi.h"
#include "ft_util.h"

namespace {

// 1. Test ft_util.h (Math utilities & Error string mapping)
TEST(FtUtilTest, DivRoundUp) {
  EXPECT_EQ(FtDivRoundUp(10, 3), 4);
  EXPECT_EQ(FtDivRoundUp(9, 3), 3);
  EXPECT_EQ(FtDivRoundUp(0, 10), 0);
  EXPECT_EQ(FtDivRoundUp(4096, 4096), 1);
}

TEST(FtUtilTest, ToStringMapping) {
  EXPECT_EQ(ToString(FtError::kSuccess), "Success");
  EXPECT_EQ(ToString(FtError::kNoSuchDevice), "No such device");
  EXPECT_EQ(ToString(FtError::kParameterError), "Parameter error");
  EXPECT_EQ(ToString(FtError::kNoSuchFile), "No such file");
  EXPECT_EQ(ToString(FtError::kResultFail), "Result fail");
  EXPECT_EQ(ToString(FtError::kInvalidMode), "Invalid mode");
}

// 2. Test ft_help.h (Help String Definition)
TEST(FtHelpTest, HelpStringNotEmpty) {
  EXPECT_FALSE(ft_help::kCmdHelpString.empty());
  EXPECT_NE(ft_help::kCmdHelpString.find("Usage:"), std::string_view::npos);
}

// 3. Test ft_scsi.h (SCSI Command CDB Building)
TEST(FtScsiTest, BuildFtCmdBasic) {
  // BuildFtCmd(opcode, subcmd, address, length, aux_address, flags)
  auto cdb = BuildFtCmd(0xDB, 0x01, 0x1000, 0x20, 0x3000, 0x80);

  ASSERT_EQ(cdb.size(), 16);
  // Verify layout matches FtCmdFormat packed struct inside ft_scsi.cc:
  // opcode (offset 0)
  EXPECT_EQ(cdb[0], 0xDB);

  // address (offset 1, 4 bytes, little endian)
  uint32_t addr = cdb[1] | (cdb[2] << 8) | (cdb[3] << 16) | (cdb[4] << 24);
  EXPECT_EQ(addr, 0x1000);

  // length (offset 5, 4 bytes, little endian)
  uint32_t len = cdb[5] | (cdb[6] << 8) | (cdb[7] << 16) | (cdb[8] << 24);
  EXPECT_EQ(len, 0x20);

  // aux_address (offset 9, 4 bytes, little endian)
  uint32_t aux = cdb[9] | (cdb[10] << 8) | (cdb[11] << 16) | (cdb[12] << 24);
  EXPECT_EQ(aux, 0x3000);

  // subcmd (offset 13)
  EXPECT_EQ(cdb[13], 0x01);

  // flags (offset 14)
  EXPECT_EQ(cdb[14], 0x80);
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}