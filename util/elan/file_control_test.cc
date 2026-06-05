// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_control.h"

#include <gtest/gtest.h>

#include <format>
#include <fstream>

namespace elan {
namespace {

TEST(FileControlTest, GetBinaryFileNotFound) {
  auto res = GetBinary("non_existent_file_xyz123.bin");
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), IapError::FileOpenFailed);
}

TEST(FileControlTest, GetBinarySuccess) {
  const std::string temp_filename = "temp_test_fw.bin";
  const std::vector<uint8_t> expected_data = {0x10, 0x20, 0x30, 0x40, 0x50};

  {
    std::ofstream ofs(temp_filename, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(ofs.is_open());
    ofs.write(reinterpret_cast<const char *>(expected_data.data()),
              expected_data.size());
  }

  auto res = GetBinary(temp_filename);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res.value(), expected_data);

  std::remove(temp_filename.c_str());
}

}  // namespace
}  // namespace elan
