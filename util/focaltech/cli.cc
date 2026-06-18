/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cli.h"

#include <cctype>
#include <cstddef>
#include <utility>

#include "updater.h"

namespace focaltech {

namespace {

using namespace std::chrono_literals;

std::expected<FlashWpArea, Error> ParseWpArea(std::string_view arg) {
  if (arg == "none") return FlashWpArea::kNone;
  if (arg == "boot") return FlashWpArea::kBoot;
  if (arg == "ro") return FlashWpArea::kRo;
  if (arg == "all") return FlashWpArea::kAll;
  return std::unexpected(Error::kInvalidParameter);
}

}  // namespace

constexpr Command ParseCommand(std::string_view cmd_str) {
  if (cmd_str == kCmdEnterRom) return Command::kEnterRom;
  if (cmd_str == kCmdGetFlashWp) return Command::kGetFlashWp;
  if (cmd_str == kCmdSetFlashWp) return Command::kSetFlashWp;
  if (cmd_str.ends_with(".bin")) return Command::kUpdate;
  return Command::kUnknown;
}

std::expected<AppConfig, Error> ParseArguments(
    std::span<const std::string_view> args) {
  if (args.empty()) {
    FT_LOGE("No subcommand specified.");
    return std::unexpected(Error::kInvalidParameter);
  }

  AppConfig config;
  const std::string_view first_arg = args[0];

  config.cmd = ParseCommand(first_arg);

  if (config.cmd == Command::kUnknown) {
    FT_LOGE("Unknown subcommand: {}", first_arg);
    return std::unexpected(Error::kInvalidParameter);
  }

  if (config.cmd == Command::kUpdate) {
    config.firmware_path = first_arg;
    return config;
  }

  if (config.cmd == Command::kSetFlashWp) {
    if (args.size() < 2) {
      FT_LOGE(
          "flash_set_protect requires an area parameter (none, boot, ro, "
          "all).");
      return std::unexpected(Error::kInvalidParameter);
    }
    const auto wp_res = ParseWpArea(args[1]);
    if (!wp_res) {
      FT_LOGE("Invalid protection area: '{}'. Must be none, boot, ro, or all.",
              args[1]);
      return std::unexpected(Error::kInvalidParameter);
    }
    config.wp_area = *wp_res;
  }

  return config;
}

std::expected<void, Error> ExecuteCommand(const AppConfig& config,
                                          UsbDevice& usb_device,
                                          DeviceMode current_mode) {
  switch (config.cmd) {
    case Command::kSetFlashWp:
      return SetFlashProtection(usb_device, config.wp_area, current_mode);

    case Command::kGetFlashWp: {
      const auto wp_res = GetFlashProtection(usb_device, current_mode);
      if (!wp_res) {
        return std::unexpected(wp_res.error());
      }
      FT_LOGI("Flash write protect area = {}", ToString(*wp_res));
      return {};
    }

    case Command::kEnterRom:
      FT_LOGI("Entering ROM mode...");
      if (current_mode != DeviceMode::kBootloader) {
        FT_LOGE(
            "enter_rom is only valid from bootloader mode. Current mode: {}",
            static_cast<int>(current_mode));
        return std::unexpected(Error::kInvalidMode);
      }
      return ReturnToRomBoot(usb_device, current_mode);

    case Command::kUpdate: {
      FT_LOGI("Updating firmware...");
      if (config.firmware_path.empty()) {
        FT_LOGE("No firmware file specified");
        return std::unexpected(Error::kInvalidParameter);
      }

      const auto fw_data_res = ReadFileToVector(config.firmware_path);
      if (!fw_data_res) {
        return std::unexpected(fw_data_res.error());
      }

      /* Fix an issue where, when the display is off but the system has not yet
       * entered suspend, the host may forcibly switch the FPMCU into suspend
       * state, causing the upgrade to fail. Retry updating the firmware. */
      for (int attempt = 1; attempt <= kMaxRetry; ++attempt) {
        const auto res = UpdateFw(usb_device, *fw_data_res, current_mode);
        if (res) {
          FT_LOGI("Firmware update successful");
          return {};
        }
        FT_LOGW("Firmware update fail attempt {}", attempt);
        if (attempt < kMaxRetry) {
          std::this_thread::sleep_for(100ms);
        }
      }
      FT_LOGE("Firmware update failed");
      return std::unexpected(Error::kHardwareFailure);
    }

    default:
      FT_LOGE("Command not implemented.");
      return std::unexpected(Error::kInvalidParameter);
  }
}

}  // namespace focaltech
