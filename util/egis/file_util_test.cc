/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "file_util.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace egis {
namespace {
TEST(FileUtilTest, ReadBinaryFileSuccess) {
  auto temp_path =
      std::filesystem::temp_directory_path() / "file_util_test.bin";
  std::ofstream(temp_path, std::ios::binary).write("DATA", 4);
  auto res = ReadBinaryFile(temp_path.string());
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->size(), 4);
  EXPECT_EQ(res.value()[0], 'D');
  std::filesystem::remove(temp_path);
}

TEST(FileUtilTest, ReadBinaryFileNotFound) {
  EXPECT_FALSE(
      ReadBinaryFile("/invalid/path/that/does/not/exist.bin").has_value());
}
}  // namespace
}  // namespace egis
