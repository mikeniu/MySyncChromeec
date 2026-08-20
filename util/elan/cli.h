// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_ELAN_CLI_H_
#define UTIL_ELAN_CLI_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "iap_control.h"

namespace elan {

struct CmdLine {
  UsbDeviceId device_id;
  std::string file_path;
  bool show_help = false;
};

std::optional<CmdLine> ParseCommandLine(int argc, char* argv[]);
void PrintHelp(const char* program_name);

namespace internal {
// Exposed for unit testing
std::optional<uint16_t> ParseHex(std::string_view strv);
}  // namespace internal

}  // namespace elan

#endif
