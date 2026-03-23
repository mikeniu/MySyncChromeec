// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <unistd.h>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "file_control.h"
#include "iap_control.h"
#include "utility.h"

struct CmdLine {
  uint16_t vid;
  uint16_t pid;
  std::string file_path;
};

void SetProgressBar(size_t progress, size_t total_size);

class CommandLineManager {
 public:
  CommandLineManager(int argc, char *argv[]) { ParseCommandLine(argc, argv); }

  const std::optional<CmdLine> &GetCommandLine() const { return cmd_line_; }

 private:
  std::optional<CmdLine> cmd_line_;

  void PrintHelp(const char *program_name) {
    std::print(R"(Usage: {} [OPTIONS]

Options:
  -F, --file <path>   Path to the firmware binary (Required)
  -V, --vid <hex>     USB Vendor ID (default: 0x04F3)
  -P, --pid <hex>     USB Product ID (default: 0x0910)
  -h, --help          Show this help message
)",
               program_name);
  }

  void ParseCommandLine(int argc, char *argv[]) {
    CmdLine cmd{elan::kBootVid, elan::kBootPid, ""};

    // Define long options
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
          PrintHelp(argv[0]);
          EXIT_SUCCESS;
        case 'F':
          cmd.file_path = optarg;
          break;
        case 'V':
          if (auto val = ParseHex(optarg))
            cmd.vid = *val;
          else {
            LogErr("Invalid VID format: {}\n", optarg);
            return;
          }
          break;
        case 'P':
          if (auto val = ParseHex(optarg))
            cmd.pid = *val;
          else {
            LogErr("Invalid PID format: {}\n", optarg);
            return;
          }
          break;
        default:
          PrintHelp(argv[0]);
          return;
      }
    }

    if (cmd.file_path.empty()) {
      LogErr("Error: Missing required argument '--file'\n");
      PrintHelp(argv[0]);
      return;
    }

    cmd_line_ = cmd;
  }

  std::optional<uint16_t> ParseHex(std::string_view strv) {
    if (strv.empty()) return std::nullopt;

    // Strip standard hex prefixes
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
};

int main(int argc, char *argv[]) {
  elan::IapControl iap_ctrl;
  CommandLineManager command_line_mgr(argc, argv);

  auto cmd_line = command_line_mgr.GetCommandLine();
  if (!cmd_line) {
    LogErr("[IAP] Failed to parse command line\n");
    return EXIT_FAILURE;
  }

  if (auto res = iap_ctrl.Initialize(cmd_line->vid, cmd_line->pid); !res) {
    LogErr("[IAP] Device initialization failed: {}\n",
           elan::ToString(res.error()));
    return EXIT_FAILURE;
  }

  LogInfo("[IAP] File Path: {}\n", cmd_line->file_path);

  // Get File Size and Content
  auto binary = GetBinary(cmd_line->file_path);
  if (!binary) {
    LogErr("[IAP] Failed to get binary: {}\n", elan::ToString(binary.error()));
    return EXIT_FAILURE;
  }

  LogInfo("[IAP] Binary file size {} bytes\n", binary->size());

  iap_ctrl.SetProgressCallback(SetProgressBar);

  // Write Rom
  if (auto res = iap_ctrl.RunIapProcess(*binary); !res) {
    LogErr("[IAP] IAP process failed: {}\n", elan::ToString(res.error()));
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

void SetProgressBar(size_t progress, size_t total_size) {
  int percent = 0;

  if (total_size == 0)
    percent = 0;
  else
    percent = (100 * (progress)) / total_size;

  LogInfo("\r[{}{}]", std::string(percent / 5, '#'),
          std::string(100 / 5 - percent / 5, ' '));
  LogInfo(" {} % [Data {} of {}]", percent, progress, total_size);
  std::fflush(stdout);

  if (percent >= 100) LogInfo("\n");
}
