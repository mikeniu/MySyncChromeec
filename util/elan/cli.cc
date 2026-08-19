// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cli.h"

#include <getopt.h>

#include <charconv>
#include <print>

#include "utility.h"

namespace elan {

void PrintHelp(const char* program_name) {
  std::print(R"(Usage: {} [OPTIONS]

Options:
  -F, --file <path>   Path to the firmware binary (Required)
  -V, --vid <hex>     USB Vendor ID (default: 0x04F3)
  -P, --pid <hex>     USB Product ID (default: 0x0910)
  -h, --help          Show this help message
)",
             program_name);
}

namespace internal {

std::optional<uint16_t> ParseHex(std::string_view strv) {
  if (strv.empty()) return std::nullopt;

  if (strv.starts_with("0x") || strv.starts_with("0X")) {
    strv.remove_prefix(2);
  }

  uint16_t val;
  auto [ptr, ec] =
      std::from_chars(strv.data(), strv.data() + strv.size(), val, 16);

  if (ec == std::errc() && ptr == strv.data() + strv.size()) {
    return val;
  }

  return std::nullopt;
}

}  // namespace internal

std::optional<CmdLine> ParseCommandLine(int argc, char* argv[]) {
  CmdLine cmd{.device_id = kBootDevice, .file_path = "", .show_help = false};

  static struct option long_options[] = {
      {"file", required_argument, nullptr, 'F'},
      {"vid", required_argument, nullptr, 'V'},
      {"pid", required_argument, nullptr, 'P'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0}};

  int opt;

  while ((opt = getopt_long(argc, argv, "hF:V:P:", long_options, nullptr)) !=
         -1) {
    switch (opt) {
      case 'h':
        cmd.show_help = true;
        return cmd;
      case 'F':
        cmd.file_path = optarg;
        break;
      case 'V': {
        if (auto val = internal::ParseHex(optarg)) {
          cmd.device_id.vid = *val;
        } else {
          LogErr("Invalid VID format: {}\n", optarg);
          return std::nullopt;
        }
        break;
      }
      case 'P': {
        if (auto val = internal::ParseHex(optarg)) {
          cmd.device_id.pid = *val;
        } else {
          LogErr("Invalid PID format: {}\n", optarg);
          return std::nullopt;
        }
        break;
      }
      default:
        PrintHelp(argv[0]);
        return std::nullopt;
    }
  }

  if (cmd.file_path.empty()) {
    LogErr("Error: Missing required argument '--file'\n");
    PrintHelp(argv[0]);
    return std::nullopt;
  }

  return cmd;
}

}  // namespace elan
