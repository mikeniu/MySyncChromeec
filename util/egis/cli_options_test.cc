/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cli_options.h"

#include <getopt.h>
#include <gtest/gtest.h>
#include <array>

namespace egis {
namespace {
// A zero-allocation helper that bridges C++ string literals with getopt's
// requirement for a mutable array of char pointers.
template <typename... Args>
std::optional<FlasherConfig> RunParseArgs(Args... args) {
  // Reset POSIX getopt global state before every test execution
  optind = 1;

  // getopt permutes the array pointers, but does not mutate the strings.
  // const_cast is safe here to satisfy the legacy C API.
  //
  // Add +1 to the size, and a trailing nullptr to conform to POSIX's getopt
  // requirement that argv[argc] == NULL.
  std::array<char*, sizeof...(args) + 1> argv = {const_cast<char*>(args)...,
                                                 nullptr};
  return ParseArgs(argv.size() - 1, argv.data());
}

TEST(CliOptionsTest, ParseHelpReturnsHelpMode) {
  auto config = RunParseArgs("et171_flash", "--help");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->mode, AppMode::kHelp);
}

TEST(CliOptionsTest, ParseValidFlashFw) {
  auto config = RunParseArgs("et171_flash", "--flashfw", "image.bin",
                             "--dumpcmd", "cmd.bin");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->mode, AppMode::kFlashFw);
  EXPECT_EQ(config->firmware_path, "image.bin");
  EXPECT_EQ(config->crypto_algorithm, EgisCryptoType::kSha256);
  EXPECT_TRUE(config->dump_cmd_path.has_value());
  EXPECT_EQ(*config->dump_cmd_path, "cmd.bin");
}

TEST(CliOptionsTest, ParseValidFlashCmd) {
  auto config = RunParseArgs("et171_flash", "--flashcmd", "cmd.bin");

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->mode, AppMode::kFlashCmd);
  EXPECT_EQ(config->firmware_path, "cmd.bin");
}

TEST(CliOptionsTest, ParseAlgorithms) {
  auto config_aes = RunParseArgs("et171_flash", "--flashfw", "img.bin",
                                 "--algo", "AESGCM", "--key", "key.bin");
  ASSERT_TRUE(config_aes.has_value());
  EXPECT_EQ(config_aes->crypto_algorithm, EgisCryptoType::kAesGcm);
  EXPECT_EQ(config_aes->key_path, "key.bin");

  EXPECT_FALSE(
      RunParseArgs("et171_flash", "--flashfw", "img.bin", "--algo", "MD5")
          .has_value());
}

TEST(CliOptionsTest, ParseOffsetAndSize) {
  auto config = RunParseArgs("et171_flash", "--flashfw", "img.bin", "--offset",
                             "0xABCD", "--size", "0xEF");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->image_offset, 0xABCD);
  EXPECT_EQ(config->firmware_size_override, 0xEF);

  EXPECT_FALSE(
      RunParseArgs("et171_flash", "--flashfw", "img.bin", "--offset", "123xyz")
          .has_value());
}

TEST(CliOptionsTest, MissingRequiredArguments) {
  EXPECT_FALSE(RunParseArgs("et171_flash").has_value());

  EXPECT_FALSE(RunParseArgs("et171_flash", "--flashfw", "img.bin", "--offset")
                   .has_value());
}

}  // namespace
}  // namespace egis
