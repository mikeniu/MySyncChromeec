/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ft_help.h"

#include <gtest/gtest.h>

#include <string_view>

namespace focaltech {

TEST(FtHelpTest, HelpStringNotEmpty) { EXPECT_FALSE(kCmdHelpString.empty()); }

TEST(FtHelpTest, HelpStringContainsExpectedCommands) {
  std::string_view help = kCmdHelpString;
  EXPECT_TRUE(help.find("enter_rom") != std::string_view::npos);
  EXPECT_TRUE(help.find("flash_get_protect") != std::string_view::npos);
  EXPECT_TRUE(help.find("flash_set_protect") != std::string_view::npos);
  EXPECT_TRUE(help.find("<firmware.bin>") != std::string_view::npos);
}

TEST(FtHelpTest, HelpStringContainsHelpOption) {
  std::string_view help = kCmdHelpString;
  EXPECT_TRUE(help.find("-h, --help") != std::string_view::npos);
  EXPECT_TRUE(help.find("Show this help message") != std::string_view::npos);
}

TEST(FtHelpTest, HelpStringContainsUsageLine) {
  std::string_view help = kCmdHelpString;
  EXPECT_TRUE(help.find("Usage: {} <command> [arguments]") !=
              std::string_view::npos);
}

}  // namespace focaltech
