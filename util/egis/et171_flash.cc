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
#include "ec_comm.h"
#include "ec_protocol_defs.h"
#include "egis_util.h"
#include "flasher_logic.h"
#include "usb_comm.h"

namespace egis {

using namespace std::chrono_literals;

namespace {

constexpr egis::UsbDeviceProfile kAppProfile = {
    .id =
        {
            .vid = 0x18D1,
            .pid = 0x5401,
        },
    .ep_out = 0x01,
    .ep_in = 0x81,
};

constexpr egis::UsbDeviceProfile kBootloaderProfile = {
    .id =
        {
            .vid = 0x1C7A,
            .pid = 0x1002,
        },
    .ep_out = 0x02,
    .ep_in = 0x81,
};

constexpr auto kBootloaderTransitionDelay = std::chrono::milliseconds(800);
constexpr auto kBootloaderConnectTimeout = std::chrono::seconds(5);

std::expected<void, EcError> ExecuteGetVersion(UsbComm &comm) {
  EcComm ec_comm(comm);
  std::println("\n--- Executing GET_VERSION ---");
  auto resp = ec_comm.SendCommandTyped<GetVersionResponse>(
      EcCommandId::kEcCmdGetVersion, 1);
  if (!resp) {
    return std::unexpected(resp.error());
  }
  GetVersionResponse response = *resp;
  std::println("Command SUCCESS. Received and parsed version info.");

  response.current_image = FromLittleEndian(response.current_image);

  std::println("\n--- Parsed Version Info ---");
  std::println("RO Version: {}", MakeStringView(response.ro_ver));
  std::println("RW Version: {}", MakeStringView(response.rw_ver));
  return {};
}

std::expected<void, EcError> ExecuteBootLoader(UsbComm &comm) {
  EcComm ec_comm(comm);
  std::println("\n--- Executing Enter Bootloader ---");
  auto resp = ec_comm.SendCommand(EcCommandId::kEcCmdEnterBootloader, 0);
  if (!resp) {
    return std::unexpected(resp.error());
  }
  return {};
}

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

std::expected<void, UsbError> ConnectToBootloader(
    libusb_context *usb_ctx, UsbComm &boot_comm,
    const UsbDeviceProfile &app_profile) {
  UsbComm app_comm(usb_ctx, app_profile);
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

  auto deadline = std::chrono::steady_clock::now() + kBootloaderConnectTimeout;
  std::println("Connecting to Bootloader...");
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto res = boot_comm.Connect(); res) {
      return {};
    }
    std::this_thread::sleep_for(200ms);
  }
  return std::unexpected(UsbError::kTimeout);
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

  egis::UsbDeviceProfile app_profile = egis::kAppProfile;
  if (config.pid.has_value()) {
    app_profile.id.pid = *config.pid;
  }

  egis::UsbDeviceProfile boot_profile = egis::kBootloaderProfile;

  egis::UsbComm boot_comm(usb_ctx.get(), boot_profile);
  if (auto res =
          egis::ConnectToBootloader(usb_ctx.get(), boot_comm, app_profile);
      !res) {
    std::println(stderr, "[ERROR] Failed to connect to bootloader: {}",
                 egis::ToString(res.error()));
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
