/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <cstdlib>
#include <print>
#include <vector>

#include "cli.h"
#include "ft_help.h"
#include "ft_log.h"
#include "libusb_transport.h"
#include "updater.h"
#include "usb_device.h"

int main(int argc, char** argv) {
  std::vector<std::string_view> args(argv + 1, argv + argc);
  if (args.empty() || args[0] == "-h" || args[0] == "--help") {
    std::println("{}", ft_help::kCmdHelpString);
    return EXIT_SUCCESS;
  }

  // 1. Parse Inputs (Logic now lives in cli.cc)
  auto config = ParseArguments(args);
  if (!config) return EXIT_FAILURE;

  // 2. Create Hardware Connection
  auto transport = focaltech::LibusbTransport::Create();
  if (!transport) {
    FT_LOGE("Failed to open USB device.");
    return EXIT_FAILURE;
  }

  // 3. Inject hardware into domain object
  UsbDevice usb_device(std::make_unique<focaltech::LibusbTransport>(
      std::move(transport.value())));

  FtDeviceInfo info;
  if (auto init_res = DeviceInit(usb_device.device_id(), info); !init_res) {
    FT_LOGE("Cannot find Focal device");
    return EXIT_FAILURE;
  }

  // 4. Execute
  if (auto exec_res = ExecuteCommand(config.value(), usb_device, info);
      !exec_res) {
    FT_LOGE("Command failed");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
