/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <print>
#include <ranges>
#include <thread>
#include <utility>

#include "bootrom.h"
#include "cli_options.h"
#include "egis_util.h"
#include "flasher_logic.h"
#include "usb_comm.h"

namespace egis {

using namespace std::chrono_literals;

namespace {

constexpr egis::UsbDeviceProfile kBootloaderProfile = {
    .id =
        {
            .vid = 0x1C7A,
            .pid = 0x1002,
        },
    .ep_out = 0x02,
    .ep_in = 0x81,
};

constexpr auto kBootloaderConnectTimeout = std::chrono::seconds(5);
constexpr auto kBootloaderConnectPollInterval = std::chrono::milliseconds(200);

std::expected<void, BootromError> PrintDeviceDiagnostics(BootromComm &brom) {
  std::println("\n--- Fetching Bootrom Information ---");
  if (auto info = brom.GetBootromInfo()) {
    std::println("Bootrom Version: 0x{:x}", info->version);
    std::println("Max Command Size: {} bytes", info->max_cmd_size);
    std::println("Max FW Upgrade Size: {} bytes", info->max_fw_upgrade_size);
  } else {
    std::println(stderr, "[ERROR] Failed to get bootrom info: {}",
                 ToString(info.error()));
  }

  std::println("\n--- Fetching Flash Information ---");
  if (auto flash = brom.GetFlashInfo()) {
    std::println("[SUCCESS] Flash Data Received:");
    std::println("  Product Name: {}", MakeStringView(flash->prod_name));
    std::println("  Flash Size: {} bytes ({} MB)", flash->flash_size,
                 flash->flash_size / 1024 / 1024);
  } else {
    std::println(stderr, "[ERROR] Failed to get flash info: {}",
                 ToString(flash.error()));
  }

  return {};
}

}  // namespace
}  // namespace egis

int main(int argc, char *argv[]) {
  // Ensure stdout is line-buffered so it perfectly interleaves with stderr
  // even when piped through adb shell or other redirections.
  setvbuf(stdout, nullptr, _IOLBF, 0);

  auto config_opt = egis::ParseArgs(argc, argv);
  if (!config_opt) {
    return EXIT_FAILURE;
  }
  const egis::AppConfig &config = *config_opt;

  if (config.mode == egis::AppMode::kHelp) {
    return EXIT_SUCCESS;
  }

  libusb_context *raw_ctx = nullptr;
  int rc = libusb_init(&raw_ctx);
  if (rc != 0) {
    std::println(stderr, "[ERROR] Failed to initialize libusb: {}",
                 libusb_error_name(rc));
    return EXIT_FAILURE;
  }
  std::unique_ptr<libusb_context, decltype(&libusb_exit)> usb_ctx(raw_ctx,
                                                                  &libusb_exit);

  egis::UsbDeviceProfile boot_profile = egis::kBootloaderProfile;
  egis::UsbComm boot_comm(usb_ctx.get(), boot_profile);
  std::println("Connecting to Bootloader...");
  auto deadline =
      std::chrono::steady_clock::now() + egis::kBootloaderConnectTimeout;
  std::expected<void, egis::UsbError> connect_res =
      std::unexpected(egis::UsbError::kTimeout);
  while (std::chrono::steady_clock::now() < deadline) {
    connect_res = boot_comm.Connect();
    if (connect_res) {
      break;
    }
    std::this_thread::sleep_for(egis::kBootloaderConnectPollInterval);
  }

  if (!connect_res) {
    std::println(stderr, "[ERROR] Failed to connect to bootloader: {}",
                 egis::ToString(connect_res.error()));
    return EXIT_FAILURE;
  }
  egis::BootromComm brom(boot_comm);
  brom.SetWrappedAlgo(config.algo_type);

  egis::PrintDeviceDiagnostics(brom);

  std::expected<void, egis::AppError> status;
  if (config.mode == egis::AppMode::kFlashFw) {
    status = egis::RunFlashFw(config, brom);
  } else if (config.mode == egis::AppMode::kFlashCmd) {
    status = egis::RunFlashCmd(config, brom);
  }

  if (status.has_value()) {
    return EXIT_SUCCESS;
  }

  return EXIT_FAILURE;
}
