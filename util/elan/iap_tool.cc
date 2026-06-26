// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>

#include "cli.h"
#include "file_control.h"
#include "iap_control.h"
#include "libusb_backend.h"
#include "utility.h"

void SetProgressBar(size_t progress, size_t total_size);

int main(int argc, char* argv[]) {
  auto cmd_line = elan::ParseCommandLine(argc, argv);
  if (!cmd_line) {
    LogErr("[IAP] Failed to parse command line\n");
    return EXIT_FAILURE;
  }

  // Handle help flag exiting here, so it doesn't crash the test runner
  if (cmd_line->show_help) {
    elan::PrintHelp(argv[0]);
    return EXIT_SUCCESS;
  }

  auto usb_backend = std::make_unique<elan::LibusbBackend>();
  elan::IapControl iap_ctrl(std::move(usb_backend));

  if (auto res = iap_ctrl.Initialize(cmd_line->device_id); !res) {
    LogErr("[IAP] Device initialization failed: {}\n",
           elan::ToString(res.error()));
    return EXIT_FAILURE;
  }

  LogInfo("[IAP] File Path: {}\n", cmd_line->file_path);

  auto binary = GetBinary(cmd_line->file_path);
  if (!binary) {
    LogErr("[IAP] Failed to get binary: {}\n", elan::ToString(binary.error()));
    return EXIT_FAILURE;
  }

  LogInfo("[IAP] Binary file size {} bytes\n", binary->size());

  iap_ctrl.SetProgressCallback(SetProgressBar);

  if (auto res = iap_ctrl.RunIapProcess(*binary); !res) {
    LogErr("[IAP] IAP process failed: {}\n", elan::ToString(res.error()));
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

void SetProgressBar(size_t progress, size_t total_size) {
  constexpr int kMaxPercent = 100;
  constexpr int kBarWidthChars = 20;
  constexpr int kPercentPerChar = kMaxPercent / kBarWidthChars;

  int percent = (total_size == 0) ? 0 : (100 * progress) / total_size;
  percent = std::min(kMaxPercent, percent);  // (Includes our earlier
                                             // bounds-check fix!)

  int filled_chars = percent / kPercentPerChar;
  int empty_chars = kBarWidthChars - filled_chars;

  LogInfo("\r[{}{}] {} % [Data {} of {}]", std::string(filled_chars, '#'),
          std::string(empty_chars, ' '), percent, progress, total_size);

  std::fflush(stdout);
  if (percent >= kMaxPercent) LogInfo("\n");
}
