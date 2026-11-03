/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_flash_cli.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace realtekfp {
namespace {

using ::testing::ElementsAreArray;

// getopt needs a mutable argv terminated by nullptr; ParseArgs() resets its
// global state before every run.
template <typename... Args>
std::expected<Config, CliError> RunParseArgs(Args... args) {
  std::array<char*, sizeof...(args) + 1> argv = {const_cast<char*>(args)...,
                                                 nullptr};

  return ParseArgs(static_cast<int>(argv.size() - 1), argv.data());
}

TEST(ParseArgsTest, ImageAndDeviceId) {
  auto cfg = RunParseArgs("rts5817_flash", "-p", "1234:abcd", "-i", "fw.bin");

  ASSERT_TRUE(cfg);
  EXPECT_EQ(cfg->dev_id, (UsbDeviceId{.vid = 0x1234, .pid = 0xABCD}));
  EXPECT_EQ(cfg->img, "fw.bin");
  EXPECT_FALSE(cfg->show_help);
}

TEST(ParseArgsTest, LongOptions) {
  auto cfg =
      RunParseArgs("rts5817_flash", "--pid", "0BDA:5817", "--img", "fw.bin");

  ASSERT_TRUE(cfg);
  EXPECT_EQ(cfg->dev_id, (UsbDeviceId{.vid = 0x0BDA, .pid = 0x5817}));
  EXPECT_EQ(cfg->img, "fw.bin");
}

TEST(ParseArgsTest, DeviceIdUnsetWhenOmitted) {
  auto cfg = RunParseArgs("rts5817_flash", "-i", "fw.bin");

  ASSERT_TRUE(cfg);
  EXPECT_FALSE(cfg->dev_id);
  EXPECT_EQ(cfg->img, "fw.bin");
}

TEST(ParseArgsTest, StateDoesNotLeakBetweenCalls) {
  ASSERT_TRUE(RunParseArgs("rts5817_flash", "-p", "0BDA:5817", "-i", "a.bin"));

  auto second = RunParseArgs("rts5817_flash", "-i", "b.bin");

  ASSERT_TRUE(second);
  EXPECT_FALSE(second->dev_id);
  EXPECT_EQ(second->img, "b.bin");
}

TEST(ParseArgsTest, ImageIsRequired) {
  auto none = RunParseArgs("rts5817_flash");
  auto only_id = RunParseArgs("rts5817_flash", "-p", "0BDA:5817");

  ASSERT_FALSE(none);
  EXPECT_EQ(none.error(), CliError::kMissingImage);
  ASSERT_FALSE(only_id);
  EXPECT_EQ(only_id.error(), CliError::kMissingImage);
}

TEST(ParseArgsTest, HelpIsNotAnError) {
  auto short_help = RunParseArgs("rts5817_flash", "-h");
  auto long_help = RunParseArgs("rts5817_flash", "--help");

  ASSERT_TRUE(short_help);
  EXPECT_TRUE(short_help->show_help);
  ASSERT_TRUE(long_help);
  EXPECT_TRUE(long_help->show_help);
}

TEST(ParseArgsTest, UnknownOptionIsAnError) {
  auto cfg = RunParseArgs("rts5817_flash", "-x", "-i", "fw.bin");

  ASSERT_FALSE(cfg);
  EXPECT_EQ(cfg.error(), CliError::kInvalidOption);
}

TEST(ParseArgsTest, OptionMissingValueIsAnError) {
  auto cfg = RunParseArgs("rts5817_flash", "-i");

  ASSERT_FALSE(cfg);
  EXPECT_EQ(cfg.error(), CliError::kInvalidOption);
}

TEST(ParseArgsTest, UnexpectedPositionalArgumentIsAnError) {
  auto cfg = RunParseArgs("rts5817_flash", "-i", "fw.bin", "extra");

  ASSERT_FALSE(cfg);
  EXPECT_EQ(cfg.error(), CliError::kUnexpectedArgument);
}

TEST(ParseArgsTest, MalformedDeviceIdIsRejectedWhileParsing) {
  auto cfg = RunParseArgs("rts5817_flash", "-p", "0BDA-5817", "-i", "fw.bin");

  ASSERT_FALSE(cfg);
  EXPECT_EQ(cfg.error(), CliError::kInvalidDeviceId);
}

TEST(ParseVidPidTest, ValidUpperAndLowerCase) {
  EXPECT_EQ(ParseVidPid("0BDA:5817"),
            (UsbDeviceId{.vid = 0x0BDA, .pid = 0x5817}));
  EXPECT_EQ(ParseVidPid("abcd:ef01"),
            (UsbDeviceId{.vid = 0xABCD, .pid = 0xEF01}));
}

TEST(ParseVidPidTest, AcceptsBoundaryValues) {
  EXPECT_EQ(ParseVidPid("0000:FFFF"),
            (UsbDeviceId{.vid = 0x0000, .pid = 0xFFFF}));
  EXPECT_EQ(ParseVidPid("FFFF:0000"),
            (UsbDeviceId{.vid = 0xFFFF, .pid = 0x0000}));
}

TEST(ParseVidPidTest, AcceptsShortFields) {
  EXPECT_EQ(ParseVidPid("1:2"), (UsbDeviceId{.vid = 0x1, .pid = 0x2}));
}

TEST(ParseVidPidTest, RejectsMalformedInput) {
  EXPECT_FALSE(ParseVidPid(""));
  EXPECT_FALSE(ParseVidPid("0BDA"));
  EXPECT_FALSE(ParseVidPid(":5817"));
  EXPECT_FALSE(ParseVidPid("0BDA:"));
  EXPECT_FALSE(ParseVidPid(":"));
  EXPECT_FALSE(ParseVidPid("0BDA-5817"));
  EXPECT_FALSE(ParseVidPid("zzzz:yyyy"));
  EXPECT_FALSE(ParseVidPid("0BDA:5817foo"));
  EXPECT_FALSE(ParseVidPid("0BDA:5817:1"));
}

TEST(ParseVidPidTest, RejectsSignsAndWhitespaceAndPrefixes) {
  EXPECT_FALSE(ParseVidPid("-0:5817"));
  EXPECT_FALSE(ParseVidPid("+0BDA:5817"));
  EXPECT_FALSE(ParseVidPid("0BDA:+5817"));
  EXPECT_FALSE(ParseVidPid(" 0BDA:5817"));
  EXPECT_FALSE(ParseVidPid("0BDA: 5817"));
  EXPECT_FALSE(ParseVidPid("0BDA:5817 "));
  EXPECT_FALSE(ParseVidPid("0x0BDA:5817"));
}

TEST(ParseVidPidTest, RejectsValuesOverSixteenBits) {
  EXPECT_FALSE(ParseVidPid("10000:5817"));
  EXPECT_FALSE(ParseVidPid("0BDA:10000"));
  // A 33-bit value must not wrap around to a valid one.
  EXPECT_FALSE(ParseVidPid("100000bda:5817"));
  EXPECT_FALSE(ParseVidPid("0BDA:-0xffff0001"));
}

class ReadFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_dir_ =
        std::filesystem::temp_directory_path() / "rts5817_flash_cli_test";
    std::filesystem::create_directories(temp_dir_);
  }

  void TearDown() override { std::filesystem::remove_all(temp_dir_); }

  std::filesystem::path WriteFile(const std::string& name,
                                  const std::vector<uint8_t>& data) {
    const auto path = temp_dir_ / name;

    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(data.data()), data.size());
    return path;
  }

  std::filesystem::path temp_dir_;
};

TEST_F(ReadFileTest, ReadsWholeBinaryFile) {
  const std::vector<uint8_t> expected = {0x00, 0xFF, 0x0A, 0x0D, 0x7F};

  auto data = ReadFile(WriteFile("fw.bin", expected));

  ASSERT_TRUE(data);
  EXPECT_THAT(*data, ElementsAreArray(expected));
}

TEST_F(ReadFileTest, EmptyFileYieldsEmptyBuffer) {
  auto data = ReadFile(WriteFile("empty.bin", {}));

  ASSERT_TRUE(data);
  EXPECT_TRUE(data->empty());
}

TEST_F(ReadFileTest, MissingFileReportsTheError) {
  auto data = ReadFile(temp_dir_ / "missing.bin");

  ASSERT_FALSE(data);
  EXPECT_EQ(data.error(), std::errc::no_such_file_or_directory);
}

TEST(CliErrorTest, ToStringCoversAllValues) {
  for (CliError error :
       {CliError::kInvalidOption, CliError::kInvalidDeviceId,
        CliError::kMissingImage, CliError::kUnexpectedArgument}) {
    EXPECT_NE(ToString(error), "Unknown error");
  }
}

}  // namespace
}  // namespace realtekfp
