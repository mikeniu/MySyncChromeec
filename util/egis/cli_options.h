/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_CLI_OPTIONS_H_
#define UTIL_EGIS_CLI_OPTIONS_H_

// C++ system files
#include <cstdint>
#include <optional>
#include <string>

// Project headers
#include "bootrom.h"

namespace egis {

enum class AppError {
  kInvalidArgs,
  kFileIoError,
  kCryptoError,
  kBootromError,
  kUsbError,
  kParseError,
};

enum class AppMode {
  kNone,
  kFlashFw,
  kFlashCmd,
};

constexpr uint32_t kDefaultFlashOffset = 0x1000;

struct AppConfig {
  AppMode mode = AppMode::kNone;
  std::string fw_path;
  std::string key_path;
  EgisCryptoType algo_type = EgisCryptoType::kSha256;
  uint32_t flash_offset = kDefaultFlashOffset;
  uint32_t fw_size_override = 0;
  bool dump_cmd_fw = false;
};

void PrintHelp(const char *prog_name);
std::optional<AppConfig> ParseArgs(int argc, char *argv[]);

}  // namespace egis

#endif  // UTIL_EGIS_CLI_OPTIONS_H_
