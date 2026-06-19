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
  FT_LOGI("Tool version is {}", focaltech::kToolVersion);

  const std::vector<std::string_view> args(argv + 1, argv + argc);
  if (args.empty() || args[0] == "-h" || args[0] == "--help") {
    std::println("{}", focaltech::kCmdHelpString);
    return EXIT_SUCCESS;
  }

  const auto config = focaltech::ParseArguments(args);
  if (!config) return EXIT_FAILURE;

  auto transport = focaltech::LibusbTransport::Create();
  if (!transport) {
    FT_LOGE("Failed to open USB device.");
    return EXIT_FAILURE;
  }

  focaltech::UsbDevice usb_device(
      std::make_unique<focaltech::LibusbTransport>(std::move(*transport)));

  const auto mode_result = focaltech::GetDeviceMode(usb_device.device_id());
  if (!mode_result) {
    FT_LOGE("Cannot find Focal device");
    return EXIT_FAILURE;
  }

  if (const auto exec_res =
          focaltech::ExecuteCommand(*config, usb_device, *mode_result);
      !exec_res) {
    FT_LOGE("Execution failed: {}", ToString(exec_res.error()));
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
