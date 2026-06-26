/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_CLI_OPTIONS_H_
#define UTIL_EGIS_CLI_OPTIONS_H_

#include <cstdint>
#include <optional>
#include <string>

#include "bootrom_protocol.h"

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

inline constexpr uint32_t kDefaultImageOffset = 0x1000;

struct FlasherConfig {
  AppMode mode = AppMode::kNone;
  std::string firmware_path;
  std::string key_path;
  EgisCryptoType crypto_algorithm = EgisCryptoType::kSha256;
  uint32_t image_offset = kDefaultImageOffset;
  uint32_t firmware_size_override = 0;
  std::optional<std::string> dump_cmd_path;
};

void PrintHelp(const char* prog_name);
std::optional<FlasherConfig> ParseArgs(int argc, char* argv[]);

}  // namespace egis

#endif  // UTIL_EGIS_CLI_OPTIONS_H_
