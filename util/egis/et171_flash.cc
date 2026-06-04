/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
// C system files
#include <cstdio>
#include <cstdlib>
#include <cstring>

// C++ system files
#include <algorithm>
#include <chrono>
#include <memory>
#include <print>
#include <ranges>
#include <thread>
#include <utility>

// Project headers
#include "bootrom.h"
#include "cli_options.h"
#include "ec_comm.h"
#include "ec_protocol_defs.h"
#include "egis_util.h"
#include "flasher_logic.h"
#include "usb_comm.h"

namespace egis {

using namespace std::chrono_literals;

namespace {

constexpr uint16_t kVidGoogle = 0x18D1;
constexpr uint16_t kPidEgisApp = 0x5401;
constexpr uint16_t kVidEgis = 0x1C7A;
constexpr uint16_t kPidEgisBootloader = 0x1002;

constexpr uint8_t kAppEpOut = 0x01;
constexpr uint8_t kAppEpIn = 0x81;
constexpr uint8_t kBootloaderEpOut = 0x02;
constexpr uint8_t kBootloaderEpIn = 0x81;

constexpr auto kBootloaderTransitionDelay = std::chrono::milliseconds(800);
constexpr auto kBootloaderConnectTimeout = std::chrono::seconds(5);
constexpr auto kBootloaderConnectPollInterval = std::chrono::milliseconds(200);

std::expected<void, EcError> ExecuteGetVersion(UsbComm &comm) {
  EcComm ec_comm(comm);
  std::println("\n--- Executing GET_VERSION ---");
  auto resp = ec_comm.SendCommandTyped<GetVersionResponse>(
      std::to_underlying(EcCommandId::kEcCmdGetVersion), 1);
  if (!resp) {
    return std::unexpected(resp.error());
  }
  GetVersionResponse response = resp.value();
  std::println("Command SUCCESS. Received and parsed version info.");

  // Correct endianness for multi-byte integer field
  response.current_image = FromLittleEndian(response.current_image);

  std::println("\n--- Parsed Version Info ---");
  std::println("RO Version: {}", MakeStringView(response.ro_ver));
  std::println("RW Version: {}", MakeStringView(response.rw_ver));
  return {};
}

std::expected<void, EcError> ExecuteBootLoader(UsbComm &comm) {
  EcComm ec_comm(comm);
  std::println("\n--- Executing Enter Bootloader ---");
  auto resp = ec_comm.SendCommand(
      std::to_underlying(EcCommandId::kEcCmdEnterBootloader), 0);
  if (!resp) {
    return std::unexpected(resp.error());
  }
  return {};
}

std::expected<void, BootromError> PrintDeviceDiagnostics(BootromComm &brom) {
  std::println("\n--- Fetching Bootrom Information ---");
  if (auto info_res = brom.GetBootromInfo(); info_res) {
    auto &info = info_res.value();
    std::println("Bootrom Version: 0x{:x}", info.version);
    std::println("Max Command Size: {} bytes", info.max_cmd_size);
    std::println("Max FW Upgrade Size: {} bytes", info.max_fw_upgrade_size);
  } else {
    std::println(stderr, "[ERROR] Failed to get bootrom info: {}",
                 ToString(info_res.error()));
  }

  std::println("\n--- Fetching Flash Information ---");
  if (auto flash_res = brom.GetFlashInfo(); flash_res) {
    auto &flash = flash_res.value();

    std::println("[SUCCESS] Flash Data Received:");
    std::println("  Product Name: {}", MakeStringView(flash.prod_name));
    std::println("  Flash Size: {} bytes ({} MB)", flash.flash_size,
                 flash.flash_size / 1024 / 1024);
  } else {
    std::println(stderr, "[ERROR] Failed to get flash info: {}",
                 ToString(flash_res.error()));
  }

  return {};
}

std::expected<void, UsbError> ConnectToBootloader(libusb_context *usb_ctx,
                                                  UsbComm &boot_comm) {
  UsbComm app_comm(usb_ctx, kVidGoogle, kPidEgisApp, kAppEpOut, kAppEpIn);
  if (auto res = app_comm.Connect(); res) {
    if (auto ver_res = ExecuteGetVersion(app_comm); !ver_res) {
      std::println(stderr, "[WARN] GET_VERSION failed: {}",
                   ToString(ver_res.error()));
    }
    std::println("--- Switching to Bootloader Mode ---");
    if (auto bl_res = ExecuteBootLoader(app_comm); !bl_res) {
      std::println(stderr, "[WARN] Enter Bootloader failed: {}",
                   ToString(bl_res.error()));
    }
    std::this_thread::sleep_for(kBootloaderTransitionDelay);
  }

  auto start_time = std::chrono::steady_clock::now();
  std::println("Connecting to Bootloader...");
  while (std::chrono::steady_clock::now() - start_time <
         kBootloaderConnectTimeout) {
    if (auto res = boot_comm.Connect(); res) {
      return {};
    }
    std::this_thread::sleep_for(kBootloaderConnectPollInterval);
  }
  return std::unexpected(UsbError::kTimeout);
}

}  // namespace
}  // namespace egis

int main(int argc, char *argv[]) {
  // 1. Parse Arguments
  auto config_opt = egis::ParseArgs(argc, argv);
  if (!config_opt) {
    return EXIT_FAILURE;
  }
  const egis::AppConfig &config = config_opt.value();

  // 2. Initialize USB Context
  libusb_context *raw_ctx = nullptr;
  int rc = libusb_init(&raw_ctx);
  if (rc != 0) {
    std::println(stderr, "[ERROR] Failed to initialize libusb: {}",
                 libusb_error_name(rc));
    return EXIT_FAILURE;
  }
  std::unique_ptr<libusb_context, decltype(&libusb_exit)> usb_ctx(raw_ctx,
                                                                  &libusb_exit);

  // 3. Connect to Device
  egis::UsbComm boot_comm(usb_ctx.get(), egis::kVidEgis,
                          egis::kPidEgisBootloader, egis::kBootloaderEpOut,
                          egis::kBootloaderEpIn);
  if (auto res = egis::ConnectToBootloader(usb_ctx.get(), boot_comm); !res) {
    std::println(stderr, "[ERROR] Failed to connect to bootloader: {}",
                 egis::ToString(res.error()));
    return EXIT_FAILURE;
  }
  egis::BootromComm brom(boot_comm);
  brom.SetWrappedAlgo(config.algo_type);

  // Print diagnostic information from the device
  egis::PrintDeviceDiagnostics(brom);

  // 4. Dispatch to Flasher
  std::expected<void, egis::AppError> status;
  if (config.mode == egis::AppMode::kFlashFw) {
    status = egis::RunFlashFw(config, brom);
  } else if (config.mode == egis::AppMode::kFlashCmd) {
    status = egis::RunFlashCmd(config, boot_comm);
  }

  if (status.has_value()) {
    return EXIT_SUCCESS;
  }

  return EXIT_FAILURE;
}
