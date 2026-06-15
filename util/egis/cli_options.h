/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_CLI_OPTIONS_H_
#define UTIL_EGIS_CLI_OPTIONS_H_

#include <cstdint>
#include <optional>
#include <string>

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
  kHelp,
};

constexpr uint32_t kDefaultFlashOffset = 0x1000;

struct AppConfig {
  AppMode mode = AppMode::kNone;
  std::string fw_path;
  std::string key_path;
  EgisCryptoType algo_type = EgisCryptoType::kSha256;
  uint32_t flash_offset = kDefaultFlashOffset;
  uint32_t fw_size_override = 0;
  std::optional<uint16_t> pid;
  std::optional<std::string> dump_cmd_path;
};

void PrintHelp(const char *prog_name);
std::optional<AppConfig> ParseArgs(int argc, char *argv[]);

}  // namespace egis

#endif  // UTIL_EGIS_CLI_OPTIONS_H_
