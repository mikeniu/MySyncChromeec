// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cli.h"

#include <getopt.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace elan {
namespace {

// --- ParseHex Tests ---

TEST(CliHexTest, ParsesHexCorrectly) {
  EXPECT_EQ(internal::ParseHex("1234"), 0x1234);
  EXPECT_EQ(internal::ParseHex("0xABCD"), 0xABCD);
  EXPECT_EQ(internal::ParseHex("0XFFFF"), 0xFFFF);
}

TEST(CliHexTest, RejectsInvalidHex) {
  EXPECT_FALSE(internal::ParseHex(""));
  EXPECT_FALSE(internal::ParseHex("0xGHIJ"));  // Invalid characters
  EXPECT_FALSE(internal::ParseHex("123456"));  // Overflow uint16_t
}

// --- ParseCommandLine Tests ---

class CliTest : public testing::Test {
 protected:
  void SetUp() override {
    // Fully reset getopt_long internal state for glibc between test runs.
    optind = 0;
  }

  // Helper to safely convert string literals to mutable char* arrays
  std::vector<char*> MakeArgv(std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);  // +1 to accommodate the null terminator
    for (auto& arg : args) {
      argv.push_back(arg.data());
    }
    // POSIX requirement: argv array must be null-terminated
    argv.push_back(nullptr);
    return argv;
  }
};

TEST_F(CliTest, ParsesValidArguments) {
  std::vector<std::string> args = {"elaniap_tool", "--file", "fw.bin", "--vid",
                                   "0x1234",       "--pid",  "ABCD"};
  auto argv = MakeArgv(args);

  auto cmd = ParseCommandLine(args.size(), argv.data());

  ASSERT_TRUE(cmd.has_value());
  EXPECT_EQ(cmd->file_path, "fw.bin");
  // Assert the entire struct at once. (Note: The extra parentheses are
  // required to prevent the preprocessor from splitting on the comma).
  EXPECT_EQ(cmd->device_id, (UsbDeviceId{.vid = 0x1234, .pid = 0xABCD}));
  EXPECT_FALSE(cmd->show_help);
}

TEST_F(CliTest, FailsOnMissingFile) {
  std::vector<std::string> args = {"elaniap_tool", "--vid", "0x1234"};
  auto argv = MakeArgv(args);

  EXPECT_FALSE(ParseCommandLine(args.size(), argv.data()).has_value());
}

TEST_F(CliTest, SetsHelpFlag) {
  std::vector<std::string> args = {"elaniap_tool", "--help"};
  auto argv = MakeArgv(args);

  auto cmd = ParseCommandLine(args.size(), argv.data());
  ASSERT_TRUE(cmd.has_value());
  EXPECT_TRUE(cmd->show_help);
}

TEST_F(CliTest, FailsOnInvalidVidHex) {
  std::vector<std::string> args = {"elaniap_tool", "--file", "fw.bin", "--vid",
                                   "NOT_HEX"};
  auto argv = MakeArgv(args);

  EXPECT_FALSE(ParseCommandLine(args.size(), argv.data()).has_value());
}

}  // namespace
}  // namespace elan
