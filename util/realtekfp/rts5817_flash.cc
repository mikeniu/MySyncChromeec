// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flashing tool for RTS5817 (Realtek USB DFU device).
//
// Self-contained tool that:
//   1. Parses VID:PID and firmware image path from command line
//   2. Enables DFU download mode on the RTS5817 device via USB control
//      transfers
//   3. Performs DFU download directly via libusb (no external dfu-util)
//
// The DFU download protocol is implemented per the USB DFU Specification
// (Interface Class 0xFE, SubClass 0x01).
//
// Dependencies: libusb-1.0 (compile-time, shared library at runtime)
//
// Usage:
//   rts5817_flash -p VID:PID -i firmware.bin
//
// Example:
//   rts5817_flash -p 0BDA:5817 -i firmware.bin

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <print>
#include <span>
#include <string_view>
#include <vector>

#include "rts5817_flash_cli.h"
#include "rts5817_flasher.h"
#include "rts5817_libusb_backend.h"
#include "rts5817_usb_device.h"

namespace {

void Report(std::string_view what, realtekfp::RtsError error) {
  std::println(stderr, "{}: {}", what, realtekfp::ToString(error));
}

}  // namespace

int main(int argc, char** argv) {
  auto cfg = realtekfp::ParseArgs(argc, argv);

  if (!cfg) {
    // ParseArgs() has already explained the problem.
    return EXIT_FAILURE;
  }
  if (cfg->show_help) {
    return EXIT_SUCCESS;
  }

  auto image = realtekfp::ReadFile(cfg->img);
  if (!image) {
    std::println(stderr, "Failed to read firmware.");
    return EXIT_FAILURE;
  }
  std::println("Firmware size: {} bytes.", image->size());

  // The device does not accept the DFU suffix as download payload.
  const std::span<const uint8_t> firmware = realtekfp::StripDfuSuffix(*image);
  if (firmware.size() != image->size()) {
    std::println("Stripped {}-byte DFU suffix, payload is {} bytes.",
                 image->size() - firmware.size(), firmware.size());
  }

  // An empty payload cannot be flashed; fail before touching the USB.
  if (firmware.empty()) {
    std::println(stderr, "Error: the firmware image is empty.");
    return EXIT_FAILURE;
  }

  realtekfp::RtsUsbDevice device(
      std::make_unique<realtekfp::RtsLibusbBackend>());

  if (auto res = realtekfp::Flash(device, cfg->dev_id, firmware); !res) {
    Report("Flash failed", res.error());
    return EXIT_FAILURE;
  }

  std::println("Flash completed successfully.");
  return EXIT_SUCCESS;
}
