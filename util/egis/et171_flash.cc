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
#include "usb_interface.h"

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

constexpr auto kBootloaderConnectTimeout = 5s;
constexpr auto kBootloaderConnectPollInterval = 200ms;

std::expected<void, BootromError> PrintDeviceDiagnostics(
    BootromComm& bootrom_comm) {
  std::println("\n--- Fetching Bootrom Information ---");
  if (auto info = bootrom_comm.GetBootromInfo()) {
    std::println("Bootrom Version: 0x{:x}", info->version);
    std::println("Max Command Size: {} bytes", info->max_cmd_size);
    std::println("Max FW Upgrade Size: {} bytes", info->max_fw_upgrade_size);
  } else {
    std::println(stderr, "[ERROR] Failed to get bootrom info: {}",
                 ToString(info.error()));
  }

  std::println("\n--- Fetching Flash Information ---");
  if (auto flash = bootrom_comm.GetFlashInfo()) {
    std::println("[SUCCESS] Flash Data Received:");
    std::println("  Product Name: {}", MakeStringView(flash->product_name));
    std::println("  Flash Size: {} bytes ({} MB)", flash->flash_size,
                 flash->flash_size / 1024 / 1024);
  } else {
    std::println(stderr, "[ERROR] Failed to get flash info: {}",
                 ToString(flash.error()));
  }

  return {};
}

class MockUsbInterface : public egis::UsbInterface {
 public:
  std::expected<void, egis::UsbError> Connect() override { return {}; }

 protected:
  std::expected<void, egis::UsbError> DoSend(
      std::span<const uint8_t> data,
      std::chrono::milliseconds timeout) override {
    return {};
  }
  std::expected<int, egis::UsbError> DoReceive(
      std::span<uint8_t> rx_buffer,
      std::chrono::milliseconds timeout) override {
    return std::unexpected(egis::UsbError::kTimeout);
  }
};

}  // namespace
}  // namespace egis

int main(int argc, char* argv[]) {
  // Ensure stdout is line-buffered so it perfectly interleaves with stderr
  // even when piped through adb shell or other redirections.
  setvbuf(stdout, nullptr, _IOLBF, 0);

  auto config_opt = egis::ParseArgs(argc, argv);
  if (!config_opt) {
    return EXIT_FAILURE;
  }
  const egis::FlasherConfig& config = *config_opt;

  if (config.mode == egis::AppMode::kHelp) {
    return EXIT_SUCCESS;
  }

  std::expected<void, egis::AppError> flash_result;
  if (config.mode == egis::AppMode::kFlashFw &&
      config.dump_cmd_path.has_value()) {
    std::println("[INFO] Dry-run mode: offline command dumping to '{}'.",
                 *config.dump_cmd_path);
    egis::MockUsbInterface mock_usb;
    egis::BootromComm bootrom_comm(mock_usb, /*dry_run=*/true);
    bootrom_comm.SetCryptoAlgorithm(config.crypto_algorithm);

    egis::PrintDeviceDiagnostics(bootrom_comm);

    flash_result = egis::ExecuteFirmwareFlash(config, bootrom_comm);
  } else {
    libusb_context* raw_ctx = nullptr;
    int rc = libusb_init(&raw_ctx);
    if (rc != 0) {
      std::println(stderr, "[ERROR] Failed to initialize libusb: {}",
                   libusb_error_name(rc));
      return EXIT_FAILURE;
    }
    std::unique_ptr<libusb_context, decltype(&libusb_exit)> usb_ctx(
        raw_ctx, &libusb_exit);

    egis::UsbDeviceProfile boot_profile = egis::kBootloaderProfile;
    egis::UsbComm usb_comm(usb_ctx.get(), boot_profile);
    std::println("Connecting to Bootloader...");
    auto deadline =
        std::chrono::steady_clock::now() + egis::kBootloaderConnectTimeout;
    std::expected<void, egis::UsbError> connect_res =
        std::unexpected(egis::UsbError::kTimeout);
    while (std::chrono::steady_clock::now() < deadline) {
      connect_res = usb_comm.Connect();
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
    egis::BootromComm bootrom_comm(usb_comm);
    bootrom_comm.SetCryptoAlgorithm(config.crypto_algorithm);

    egis::PrintDeviceDiagnostics(bootrom_comm);

    if (config.mode == egis::AppMode::kFlashFw) {
      flash_result = egis::ExecuteFirmwareFlash(config, bootrom_comm);
    } else if (config.mode == egis::AppMode::kFlashCmd) {
      flash_result = egis::ExecuteRawCommandFlash(config, bootrom_comm);
    }
  }

  if (flash_result.has_value()) {
    return EXIT_SUCCESS;
  }

  return EXIT_FAILURE;
}
