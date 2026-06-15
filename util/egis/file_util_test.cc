/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "file_util.h"

#include "crypto_util.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace egis {
namespace {

class FileUtilTest : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_dir_ = std::filesystem::temp_directory_path() / "egis_file_test";
    std::filesystem::create_directories(temp_dir_);
  }

  void TearDown() override {
    if (std::filesystem::exists(temp_dir_)) {
      std::filesystem::remove_all(temp_dir_);
    }
  }

  std::string WriteTestFile(std::string_view filename,
                            std::span<const uint8_t> data) {
    auto path = temp_dir_ / filename;
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(data.data()), data.size());
    return path.string();
  }

  std::filesystem::path temp_dir_;
};

TEST_F(FileUtilTest, ReadBinaryFileSuccess) {
  const auto test_data = std::to_array<uint8_t>({'D', 'A', 'T', 'A'});
  std::string path = WriteTestFile("test.bin", test_data);

  auto res = ReadBinaryFile(path);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->size(), 4);
  EXPECT_THAT(*res, ::testing::ElementsAreArray(test_data));
}

TEST_F(FileUtilTest, ReadBinaryFileNotFound) {
  auto bad_path = (temp_dir_ / "does_not_exist.bin").string();
  auto res = ReadBinaryFile(bad_path);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), std::errc::no_such_file_or_directory);
}

TEST_F(FileUtilTest, ReadEmptyFileFails) {
  std::string path = WriteTestFile("empty.bin", {});
  auto res = ReadBinaryFile(path);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), std::errc::invalid_argument);
}

TEST_F(FileUtilTest, LoadAesKeySuccess) {
  std::vector<uint8_t> valid_key_data(crypto::kAes256KeySize, 0xAB);
  std::string path = WriteTestFile("valid_key.bin", valid_key_data);

  auto res = LoadAesKey(path);

  ASSERT_TRUE(res.has_value());

  EXPECT_THAT(*res, ::testing::ElementsAreArray(valid_key_data));
}

TEST_F(FileUtilTest, LoadAesKeyFailsIfTooSmall) {
  std::vector<uint8_t> short_key_data(crypto::kAes256KeySize - 1, 0xAB);
  std::string path = WriteTestFile("short_key.bin", short_key_data);

  auto res = LoadAesKey(path);

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), std::errc::invalid_argument);
}

TEST_F(FileUtilTest, LoadAesKeyFailsIfTooLarge) {
  std::vector<uint8_t> large_key_data(crypto::kAes256KeySize + 1, 0xAB);
  std::string path = WriteTestFile("large_key.bin", large_key_data);

  auto res = LoadAesKey(path);

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), std::errc::invalid_argument);
}

TEST_F(FileUtilTest, LoadAesKeyFailsIfFileNotExists) {
  auto bad_path = (temp_dir_ / "non_existent_key.bin").string();

  auto res = LoadAesKey(bad_path);

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), std::errc::no_such_file_or_directory);
}

}  // namespace
}  // namespace egis
