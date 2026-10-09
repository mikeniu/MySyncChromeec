/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cli_options.h"

#include <getopt.h>

#include <charconv>
#include <concepts>
#include <print>
#include <string_view>
#include <system_error>

namespace egis {

namespace {
template <std::integral T>
std::expected<T, std::errc> ParseHex(std::string_view hex_string) {
  if (hex_string.empty()) return std::unexpected(std::errc::invalid_argument);

  if (hex_string.starts_with("0x") || hex_string.starts_with("0X")) {
    hex_string.remove_prefix(2);
  }

  if (hex_string.empty()) return std::unexpected(std::errc::invalid_argument);

  T val;
  auto [ptr, ec] = std::from_chars(
      hex_string.data(), hex_string.data() + hex_string.size(), val, 16);

  if (ec != std::errc{}) {
    return std::unexpected(ec);
  }

  // Catch trailing garbage (e.g., "1000xyz")
  if (ptr != hex_string.data() + hex_string.size()) {
    return std::unexpected(std::errc::invalid_argument);
  }

  return val;
}
}  // namespace

void PrintHelp(const char* prog_name) {
  std::print(
      "\n=== ET171 Flash Tool ===\n\n"
      "Usage:\n"
      "  {0} <mode> [options]\n\n"
      "Modes (mutually exclusive):\n"
      "  --flashfw, -f <fw.bin>  Flash firmware with real-time encryption.\n"
      "  --flashcmd, -c <cmd.bin>\n"
      "                          Flash a pre-processed raw command binary.\n"
      "Options:\n"
      "  --algo, -a <type>       Encryption algorithm: SHA256 (default) or "
      "AESGCM.\n"
      "  --key, -k <key.bin>     Path to the AES-GCM key file (required for "
      "AESGCM).\n"
      "  --offset, -o <addr>     Start offset in hex (defines both flash "
      "address\n"
      "                          and file bytes to skip. Default: "
      "0x00001000).\n"
      "  --size, -s <bytes>      Size to flash in hex (Default: entire "
      "firmware file).\n"
      "  --dumpcmd, -d <file>    Save processed commands to a file (for "
      "--flashfw mode).\n"
      "  --help, -h              Show this help message.\n\n"
      "Examples:\n"
      "  {0} --flashfw fw.bin --algo AESGCM --key key.bin "
      "--dumpcmd cmd.bin\n"
      "  {0} --flashcmd cmd.bin\n",
      prog_name);
}

std::optional<FlasherConfig> ParseArgs(int argc, char* argv[]) {
  FlasherConfig config;

  static constexpr option long_options[] = {
      {.name = "flashfw",
       .has_arg = required_argument,
       .flag = nullptr,
       .val = 'f'},
      {.name = "flashcmd",
       .has_arg = required_argument,
       .flag = nullptr,
       .val = 'c'},
      {.name = "algo",
       .has_arg = required_argument,
       .flag = nullptr,
       .val = 'a'},
      {.name = "key",
       .has_arg = required_argument,
       .flag = nullptr,
       .val = 'k'},
      {.name = "offset",
       .has_arg = required_argument,
       .flag = nullptr,
       .val = 'o'},
      {.name = "size",
       .has_arg = required_argument,
       .flag = nullptr,
       .val = 's'},
      {.name = "dumpcmd",
       .has_arg = required_argument,
       .flag = nullptr,
       .val = 'd'},
      {.name = "help", .has_arg = no_argument, .flag = nullptr, .val = 'h'},
      {nullptr, 0, nullptr, 0}};

  while (true) {
    int opt =
        getopt_long_only(argc, argv, "f:c:a:k:o:s:d:h", long_options, nullptr);
    if (opt == -1) {
      break;
    }
    switch (opt) {
      case 'f':
        config.mode = AppMode::kFlashFw;
        config.firmware_path = optarg;
        break;
      case 'c':
        config.mode = AppMode::kFlashCmd;
        config.firmware_path = optarg;
        break;
      case 'a':
        if (std::string_view(optarg) == "SHA256")
          config.crypto_algorithm = EgisCryptoType::kSha256;
        else if (std::string_view(optarg) == "AESGCM")
          config.crypto_algorithm = EgisCryptoType::kAesGcm;
        else
          return std::nullopt;
        break;
      case 'k':
        config.key_path = optarg;
        break;
      case 'o':
      case 's': {
        auto parsed_val = ParseHex<uint32_t>(optarg);
        if (!parsed_val) return std::nullopt;
        if (opt == 'o') config.image_offset = *parsed_val;
        if (opt == 's') config.firmware_size_override = *parsed_val;
        break;
      }
      case 'd':
        config.dump_cmd_path = optarg;
        break;
      case 'h':
        PrintHelp(argv[0]);
        config.mode = AppMode::kHelp;
        return config;
      default:
        // getopt_long_only returns '?' for unknown options or missing args.
        // In either case, we should fail parsing.
        return std::nullopt;
    }
  }
  if (config.mode == AppMode::kNone) return std::nullopt;
  return config;
}

}  // namespace egis
