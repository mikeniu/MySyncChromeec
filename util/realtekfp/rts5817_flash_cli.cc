/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_flash_cli.h"

#include <getopt.h>

#include <cerrno>
#include <charconv>
#include <cstdio>
#include <expected>
#include <fstream>
#include <ios>
#include <print>
#include <string_view>
#include <system_error>

namespace realtekfp {

namespace {

void Usage(const char* prog) {
  std::println(stderr,
               "Usage: {0} -p VID:PID -i firmware.bin\n"
               "\n"
               "Flash firmware (via DFU) to an RTS5817 USB device.\n"
               "\n"
               "Options:\n"
               "  -p, --pid   VID:PID of the target device (e.g., 0BDA:5817)\n"
               "  -i, --img   Path to the firmware binary\n"
               "  -h, --help  Show this help\n"
               "\n"
               "Example:\n"
               "  {0} -p 0BDA:5817 -i firmware.bin",
               prog);
}

// Parses one hexadecimal field. from_chars rejects leading whitespace, signs
// and a "0x" prefix, and reports values that do not fit in 16 bits.
std::optional<uint16_t> ParseHex16(std::string_view text) {
  uint16_t value = 0;
  const char* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(text.data(), end, value, 16);

  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

std::string_view ToString(CliError error) {
  switch (error) {
    case CliError::kInvalidOption:
      return "Invalid option";
    case CliError::kInvalidDeviceId:
      return "Malformed VID:PID";
    case CliError::kMissingImage:
      return "--img (-i) is required";
    case CliError::kUnexpectedArgument:
      return "Unexpected positional argument";
  }
  return "Unknown error";
}

std::expected<std::vector<uint8_t>, std::errc> ReadFile(
    const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    const int saved_errno = errno;

    std::println(stderr, "Error: cannot open '{}' for reading: {}",
                 path.string(), std::generic_category().message(saved_errno));
    return std::unexpected(static_cast<std::errc>(saved_errno));
  }

  const std::streampos pos = file.tellg();
  if (pos < 0) {
    std::println(stderr, "Error: cannot determine file size for '{}'",
                 path.string());
    return std::unexpected(std::errc::io_error);
  }
  const auto size = static_cast<size_t>(pos);
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> data(size);
  file.read(reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(size));

  if (!file) {
    std::println(stderr, "Error: short read from '{}'", path.string());
    return std::unexpected(std::errc::io_error);
  }
  return data;
}

std::optional<UsbDeviceId> ParseVidPid(std::string_view dev_id) {
  const size_t colon = dev_id.find(':');
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }

  const auto vid = ParseHex16(dev_id.substr(0, colon));
  const auto pid = ParseHex16(dev_id.substr(colon + 1));
  if (!vid || !pid) {
    return std::nullopt;
  }
  return UsbDeviceId{.vid = *vid, .pid = *pid};
}

std::expected<Config, CliError> ParseArgs(int argc, char** argv) {
  static const struct option long_opts[] = {
      {"pid", required_argument, nullptr, 'p'},
      {"img", required_argument, nullptr, 'i'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };

  Config cfg;
  int opt;

  optind = 1;
  while ((opt = getopt_long(argc, argv, "p:i:h", long_opts, nullptr)) != -1) {
    switch (opt) {
      case 'p': {
        const auto dev_id = ParseVidPid(optarg);

        if (!dev_id) {
          std::println(stderr, "Error: cannot parse VID:PID '{}'.", optarg);
          return std::unexpected(CliError::kInvalidDeviceId);
        }
        cfg.dev_id = *dev_id;
        break;
      }
      case 'i':
        cfg.img = optarg;
        break;
      case 'h':
        Usage(argv[0]);
        cfg.show_help = true;
        return cfg;
      default:
        Usage(argv[0]);
        return std::unexpected(CliError::kInvalidOption);
    }
  }

  if (optind < argc) {
    std::println(stderr, "Error: unexpected positional argument '{}'.",
                 argv[optind]);
    Usage(argv[0]);
    return std::unexpected(CliError::kUnexpectedArgument);
  }

  if (cfg.img.empty()) {
    std::println(stderr, "Error: --img (-i) is required.");
    Usage(argv[0]);
    return std::unexpected(CliError::kMissingImage);
  }
  return cfg;
}

}  // namespace realtekfp
