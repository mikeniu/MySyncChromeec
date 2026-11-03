/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// Command line and file helpers for the RTS5817 flashing tool.

#ifndef PLATFORM_EC_UTIL_REALTEKFP_RTS5817_FLASH_CLI_H_
#define PLATFORM_EC_UTIL_REALTEKFP_RTS5817_FLASH_CLI_H_

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

#include "rts5817_usb_backend.h"

namespace realtekfp {

struct Config {
  // Target device; unset means the tool picks the default.
  std::optional<UsbDeviceId> dev_id;
  // Firmware image path.
  std::filesystem::path img;
  // --help was requested; not an error.
  bool show_help = false;
};

enum class CliError {
  kInvalidOption,
  kInvalidDeviceId,
  kMissingImage,
  kUnexpectedArgument,
};

std::string_view ToString(CliError error);

// Reads an entire binary file.
std::expected<std::vector<uint8_t>, std::errc> ReadFile(
    const std::filesystem::path& path);

// Parses the command line. Resets getopt state, so it can be called more than
// once. With --help it succeeds with `show_help` set and prints the usage.
std::expected<Config, CliError> ParseArgs(int argc, char** argv);

// Parses a "VID:PID" string (hexadecimal, at most four digits per field).
std::optional<UsbDeviceId> ParseVidPid(std::string_view dev_id);

}  // namespace realtekfp

#endif  // PLATFORM_EC_UTIL_REALTEKFP_RTS5817_FLASH_CLI_H_
