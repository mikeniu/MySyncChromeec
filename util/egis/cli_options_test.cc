/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cli_options.h"

#include <gtest/gtest.h>

namespace egis {
namespace {
TEST(CliOptionsTest, ParseHelpReturnsNullopt) {
  char cmd[] = "et171_flash";
  char flag[] = "--help";
  char* argv[] = {cmd, flag};
  EXPECT_FALSE(ParseArgs(2, argv).has_value());
}

TEST(CliOptionsTest, ParseValidFlashFw) {
  char cmd[] = "et171_flash";
  char flag[] = "--flashfw";
  char arg[] = "image.bin";
  char* argv[] = {cmd, flag, arg};
  auto config = ParseArgs(3, argv);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->mode, AppMode::kFlashFw);
  EXPECT_EQ(config->fw_path, "image.bin");
  EXPECT_EQ(config->algo_type, EgisCryptoType::kSha256);
}
}  // namespace
}  // namespace egis
