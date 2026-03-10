/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cli.h"

#include "updater.h"

using namespace std::chrono_literals;
constexpr auto kFastUsbTimeout = 200ms;
constexpr auto kMcuRebootPollInterval = 10ms;
constexpr auto kResetWaitTimeout = 3s;

// Commands sent to the EC‑mode device via direct bulk transfer.
constexpr std::array<uint8_t, 8> kTestEcSwitchToRomCmd = {
    0x03, 0x00, 0xCE, 0xFA, 0x00, 0xAA, 0x06, 0x00};
constexpr std::array<uint8_t, 8> kSwitchToBootCmd = {0x03, 0x1B, 0xE2, 0x00,
                                                     0x00, 0x00, 0x00, 0x00};
constexpr std::array<uint8_t, 8> kGetBuildVersionCmd = {0x03, 0xF9, 0x04, 0x00,
                                                        0x00, 0x00, 0x00, 0x00};
constexpr size_t kEcResponseHeaderSize = 8;

constexpr Command ParseCommand(std::string_view cmd_str) {
  if (cmd_str == "enter_bootloader") return Command::kEnterBoot;
  if (cmd_str == "enter_rom") return Command::kEnterRom;
  if (cmd_str == "flash_get_protect") return Command::kGetFlashWp;
  if (cmd_str == "flash_set_protect") return Command::kSetFlashWp;
  if (cmd_str == "get_ec_version") return Command::kGetEcVersion;
  if (cmd_str.ends_with(".bin")) return Command::kUpdate;

  return Command::kUnknown;
}

// Queries the EC build version string and checks that it's a Focal device.
// Queries the EC build version string and checks that it's a Focal device.
std::expected<void, FtError> CheckEcBuildVersion(UsbDevice& usb,
                                                 FtDeviceMode current_mode) {
  constexpr size_t kRxLen = 64;
  std::array<uint8_t, kRxLen> rx{};
  constexpr int kRetries = kMaxRetry;
  auto recv_result = std::expected<size_t, FtError>();

  if (current_mode != FtDeviceMode::kTest &&
      current_mode != FtDeviceMode::kEc) {
    FT_LOGE("Invalid mode for fw version get");
    return std::unexpected(FtError::kResultFail);
  }

  for (int i = 0; i < kRetries; ++i) {
    auto sr = usb.Send(kGetBuildVersionCmd, kFastUsbTimeout);
    if (!sr.has_value() || sr.value() <= 0) {
      FT_LOGW("Send build-version cmd failed.");
      std::this_thread::sleep_for(kMcuRebootPollInterval);
      continue;
    }

    recv_result = usb.Recv(rx, kFastUsbTimeout);
    if (recv_result.has_value() && recv_result.value() > 0) {
      break;
    }
    FT_LOGW("Recv build-version failed.");
    std::this_thread::sleep_for(kMcuRebootPollInterval);
  }

  // Ensure we actually received data
  if (!recv_result.has_value() ||
      recv_result.value() <= kEcResponseHeaderSize) {
    FT_LOGE("Failed to receive build version after retries");
    return std::unexpected(FtError::kResultFail);
  }

  // Version string starts at byte 8.
  size_t valid_data_len =
      std::max<size_t>(0, recv_result.value() - kEcResponseHeaderSize);
  std::string_view info(
      reinterpret_cast<const char*>(rx.data() + kEcResponseHeaderSize),
      valid_data_len);
  FT_LOGI("EC build version: {}", info);

  bool is_focal_device =
      std::any_of(kSupportedProjects.begin(), kSupportedProjects.end(),
                  [&info](std::string_view project) {
                    return info.find(project) != std::string_view::npos;
                  });

  if (!is_focal_device) {
    FT_LOGE("can't find focal project device");
    return std::unexpected(FtError::kNoSuchDevice);
  }
  return {};
}

// Generic helper to send a mode-switch command and wait for the MCU to reset
std::expected<void, FtError> SwitchMcuMode(UsbDevice& usb,
                                           FtDeviceMode current_mode,
                                           std::span<const uint8_t> switch_cmd,
                                           std::string_view mode_name) {
  if (auto res = CheckEcBuildVersion(usb, current_mode); !res) {
    FT_LOGE("Cannot get ec build version");
    return std::unexpected(FtError::kResultFail);
  }

  std::array<uint8_t, 4> rx{};

  for (int attempt = 1; attempt <= kMaxRetry; ++attempt) {
    auto sr = usb.Send(switch_cmd, kFastUsbTimeout);
    if (!sr) {
      if (sr.error() == FtError::kNoSuchDevice) {
        FT_LOGW("USB device lost (expected during {} switch).", mode_name);
        return {};
      }
      FT_LOGW("MCU reset to {} — send failed.", mode_name);
    }

    auto rr = usb.Recv(rx, kFastUsbTimeout);
    if (rr && *rr > 0) {
      FT_LOGW("MCU reset — received {} bytes.", *rr);
      break;
    }

    if (!rr && rr.error() == FtError::kNoSuchDevice) {
      FT_LOGW("USB device lost (expected during {} switch).", mode_name);
      return {};
    }

    std::this_thread::sleep_for(kMcuRebootPollInterval);
  }

  if (auto res = usb.WaitForReset(kResetWaitTimeout); !res) {
    return std::unexpected(FtError::kResultFail);
  }
  return {};
}

// Helper function for safe string_view to int conversion
std::expected<int, FtError> ParseInt(std::string_view str) {
  int value = 0;
  auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);

  // Ensure there were no parsing errors AND that the whole string was consumed
  if (ec != std::errc() || ptr != str.data() + str.size()) {
    return std::unexpected(FtError::kParameterError);
  }

  return value;
}

std::expected<AppConfig, FtError> ParseArguments(
    std::span<const std::string_view> args) {
  AppConfig config{
      .cmd = ParseCommand(args[0]),
  };

  if (args.empty()) {
    FT_LOGE("No subcommand specified.");
    return std::unexpected(FtError::kParameterError);
  }

  if (config.cmd == Command::kUnknown) {
    FT_LOGE("Unknown subcommand: {}", args[0]);
    return std::unexpected(FtError::kParameterError);
  }

  if (config.cmd == Command::kUpdate) {
    if (args.size() < 1) {
      FT_LOGE("Update command requires firmware file (.bin)");
      return std::unexpected(FtError::kParameterError);
    }
    config.ec_bin = args[0];
  } else if (config.cmd == Command::kSetFlashWp) {
    if (args.size() < 2) {
      FT_LOGE("flash_set_protect requires an area parameter (1-4).");
      return std::unexpected(FtError::kParameterError);
    }

    auto param_res = ParseInt(args[1]);
    if (!param_res) {
      FT_LOGE("Invalid integer format for flash_set_protect parameter: {}",
              args[1]);
      return std::unexpected(param_res.error());
    }

    config.action_param = *param_res;
  }

  return config;
}

// Initializes device info based on USB descriptor to determine current mode.
std::expected<void, FtError> DeviceInit(const UsbDeviceId& details,
                                        FtDeviceInfo& info) {
  switch (details.vid) {
    case kFocalRomVid:
      info.current_mode = FtDeviceMode::kRom;
      break;
    case kFocalBootVid:
      info.current_mode = FtDeviceMode::kBoot;
      break;
    case kFocalEcVid:
      info.current_mode = (details.pid == kFocalEcPidTest) ? FtDeviceMode::kTest
                                                           : FtDeviceMode::kEc;
      break;
    default:
      info.current_mode = FtDeviceMode::kUnknown;
      break;
  }

  if (info.current_mode == FtDeviceMode::kUnknown) {
    FT_LOGE("Unrecognized Focal device.");
    return std::unexpected(FtError::kNoSuchDevice);
  }
  return {};
}

// Helper function to route the parsed command to the correct operation
std::expected<void, FtError> ExecuteCommand(const AppConfig& config,
                                            UsbDevice& usb_device,
                                            FtDeviceInfo& info) {
  switch (config.cmd) {
    case Command::kGetEcVersion:
      FT_LOGI("Tool version is v1.0.3");
      return CheckEcBuildVersion(usb_device, info.current_mode);

    case Command::kSetFlashWp:
      return FtSetFlashProtect(usb_device,
                               static_cast<FtFlashWpArea>(config.action_param),
                               info.current_mode);

    case Command::kGetFlashWp: {
      auto wp_res = FtGetFlashProtect(usb_device, info.current_mode);
      if (!wp_res) {
        return std::unexpected(wp_res.error());
      }
      FT_LOGI("Flash write protect area = {}", static_cast<int>(*wp_res));
      return {};
    }

    case Command::kEnterBoot:
      FT_LOGI("Entering bootloader mode...");
      return SwitchMcuMode(usb_device, info.current_mode, kSwitchToBootCmd,
                           "boot");

    case Command::kEnterRom:
      FT_LOGI("Entering ROM mode...");
      // Route to the correct ROM switch function based on the current mode
      if (info.current_mode == FtDeviceMode::kTest) {
        return SwitchMcuMode(usb_device, info.current_mode,
                             kTestEcSwitchToRomCmd, "ROM");
      }
      return FtBackToRomBoot(usb_device, info.current_mode);

    case Command::kUpdate:
      FT_LOGI("Updating firmware...");
      info.ec_bin = config.ec_bin;
      if (info.ec_bin.empty()) {
        FT_LOGE("No firmware file specified");
        return std::unexpected(FtError::kParameterError);
      }

      /*Fix an issue where, when the display is off but the system has not yet
       * entered suspend, the host may forcibly switch the FPMCU into suspend
       * state, causing the upgrade to fail. Retry updating the firmware.*/
      for (int attempt = 1; attempt <= kMaxRetry; ++attempt) {
        if (auto res = UpdateFw(usb_device, info); res) {
          FT_LOGI("Firmware update successful");
          return {};
        }
        FT_LOGW("Firmware update fail attempt {}", attempt);
      }
      FT_LOGE("Firmware update failed");
      return std::unexpected(FtError::kResultFail);

    default:
      FT_LOGE("Command not implemented.");
      return std::unexpected(FtError::kParameterError);
  }
}
