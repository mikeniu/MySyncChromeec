/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "file_control.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace elan {
namespace {

struct GetBinaryTestCase {
  std::string test_name;
  // std::nullopt = missing file. Empty vector = 0-byte file.
  std::optional<std::vector<uint8_t>> file_content;
  elan::IapError expected_error;
};

class FileControlParamTest : public testing::TestWithParam<GetBinaryTestCase> {
 protected:
  void SetUp() override {
    const auto* info = testing::UnitTest::GetInstance()->current_test_info();

    std::string filename = info->name();
    std::replace(filename.begin(), filename.end(), '/', '_');

    temp_filepath_ = std::filesystem::path(testing::TempDir()) /
                     std::format("test_fw_{}.bin", filename);
  }

  void TearDown() override { std::filesystem::remove(temp_filepath_); }

  void WriteTestData(std::span<const uint8_t> data) {
    std::ofstream ofs(temp_filepath_, std::ios::binary);
    ASSERT_TRUE(ofs.is_open());
    ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
  }

  std::filesystem::path temp_filepath_;
};

TEST_P(FileControlParamTest, ValidatesFileHandling) {
  const GetBinaryTestCase& param = GetParam();

  // 1. Setup: Only create the file if the optional contains a value
  if (param.file_content.has_value()) {
    WriteTestData(*param.file_content);
  }

  // 2. Execute
  auto res = GetBinary(temp_filepath_.string());

  // 3. Assert
  if (param.expected_error == elan::IapError::None) {
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res.value(), *param.file_content);
  } else {
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), param.expected_error);
  }
}

INSTANTIATE_TEST_SUITE_P(
    FileSizesAndAlignments, FileControlParamTest,
    testing::Values(
        GetBinaryTestCase{
            .test_name = "Aligned4Bytes",
            .file_content = std::vector<uint8_t>{0x10, 0x20, 0x30, 0x40},
            .expected_error = elan::IapError::None},
        GetBinaryTestCase{.test_name = "EmptyFile",
                          .file_content = std::vector<uint8_t>{},
                          .expected_error = elan::IapError::InvalidFileSize},
        GetBinaryTestCase{.test_name = "MissingFile",
                          .file_content = std::nullopt,
                          .expected_error = elan::IapError::FileOpenFailed}),
    [](const testing::TestParamInfo<GetBinaryTestCase>& info) {
      return info.param.test_name;
    });

}  // namespace
}  // namespace elan
