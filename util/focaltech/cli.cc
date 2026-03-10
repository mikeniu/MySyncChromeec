/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cli.h"

#include <ec_commands.h>

#include <utility>

#include "updater.h"

namespace {

using namespace std::chrono_literals;
constexpr auto kMcuRebootPollInterval = 10ms;
constexpr auto kResetWaitTimeout = 3s;
// Default timeout for EC commands
constexpr auto kEcCommandTimeout = std::chrono::milliseconds(200);

constexpr uint16_t kTestEcSwitchToRomCmd = 0xFACE;
// Vendor-specific magic sequence required to force the MCU into ROM mode.
constexpr std::array<uint8_t, 3> kSwitchToRomPayload = {0xAA, 0x06, 0x00};

// Build EC command packet (protocol version 3)
std::vector<uint8_t> BuildEcCommand(uint16_t command, uint8_t version,
                                    std::span<const uint8_t> params = {}) {
  ec_host_request req{
      .struct_version = EC_HOST_REQUEST_VERSION,
      .checksum = 0,
      .command = command,
      .command_version = version,
      .reserved = 0,
      .data_len = static_cast<uint16_t>(params.size()),
  };

  std::vector<uint8_t> packet(sizeof(req) + params.size());
  memcpy(packet.data(), &req, sizeof(req));
  if (!params.empty()) {
    memcpy(packet.data() + sizeof(req), params.data(), params.size());
  }

  // Calculate checksum: sum of all bytes must be 0
  uint8_t checksum = 0;
  for (uint8_t b : packet) checksum += b;

  uint8_t* checksum_ptr = packet.data() + offsetof(ec_host_request, checksum);
  *checksum_ptr = -checksum;
  return packet;
}

// Send EC command and read response using UsbDevice
// Handles the case where Recv returns more bytes than requested (e.g., 64-byte
// USB packets)
std::expected<std::vector<uint8_t>, FtError> SendEcCommandAndReadResponse(
    UsbDevice& device, uint16_t command, uint8_t version,
    std::span<const uint8_t> params = {}) {
  constexpr size_t kMaxPacketSize = 64;
  auto packet = BuildEcCommand(command, version, params);

  // Send the request packet
  auto send_result = device.Send(packet, kEcCommandTimeout);
  if (!send_result) {
    if (send_result.error() == FtError::kNoSuchDevice) {
      return std::unexpected(FtError::kNoSuchDevice);
    }
    return std::unexpected(send_result.error());
  }

  // Read initial chunk of data (may be larger than header)
  std::array<uint8_t, kMaxPacketSize> initial_buffer;
  auto recv_initial = device.Recv(initial_buffer, kEcCommandTimeout);
  if (!recv_initial) {
    if (recv_initial.error() == FtError::kNoSuchDevice) {
      return std::unexpected(FtError::kNoSuchDevice);
    }
    return std::unexpected(FtError::kResultFail);
  }

  size_t received = *recv_initial;
  if (received < sizeof(ec_host_response)) {
    FT_LOGE("Insufficient data for EC response header: got {} bytes, need {}",
            received, sizeof(ec_host_response));
    return std::unexpected(FtError::kResultFail);
  }

  // Extract response header from the initial chunk
  ec_host_response resp;
  memcpy(&resp, initial_buffer.data(), sizeof(resp));
  FT_LOGD("resp.data_len = {}", resp.data_len);

  // Build the complete response data: initial chunk after header + any
  // remaining data
  std::vector<uint8_t> response_data;
  size_t header_size = sizeof(ec_host_response);
  size_t data_already_received = received - header_size;
  if (data_already_received > 0) {
    response_data.insert(response_data.end(),
                         initial_buffer.begin() + header_size,
                         initial_buffer.begin() + received);
  }

  // If we still need more data to reach resp.data_len, read the rest
  if (response_data.size() < resp.data_len) {
    size_t remaining = resp.data_len - response_data.size();
    std::vector<uint8_t> additional(remaining);
    auto recv_more = device.Recv(additional, kEcCommandTimeout);
    if (!recv_more || std::cmp_not_equal(*recv_more, remaining)) {
      FT_LOGE(
          "Failed to read remaining response data: expected {} bytes, got {}",
          remaining, recv_more.has_value() ? *recv_more : -1);
      return std::unexpected(FtError::kResultFail);
    }
    response_data.insert(response_data.end(), additional.begin(),
                         additional.end());
  }

  return response_data;
}

}  // namespace

// ============================================================================
// Public EC command functions
// ============================================================================

// 1. Get build version string
std::expected<std::string, FtError> GetBuildInfo(UsbDevice& device) {
  auto data =
      SendEcCommandAndReadResponse(device, EC_CMD_GET_BUILD_INFO, 0, {});
  if (!data) {
    return std::unexpected(data.error());
  }
  // Scan from the front to find the first null terminator.
  // If none exists, end_it will safely point to data->end().
  auto end_it = std::ranges::find(*data, '\0');
  return std::string(data->begin(), end_it);
}

// 2. Switch to bootloader mode
std::expected<void, FtError> SwitchToBootloader(UsbDevice& device,
                                                uint8_t mode) {
  ec_params_enter_bootloader params{
      .mode = mode,
  };
  auto params_span = std::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(&params), sizeof(params));
  auto data = SendEcCommandAndReadResponse(device, EC_CMD_ENTER_BOOTLOADER, 0,
                                           params_span);
  if (!data) {
    return std::unexpected(data.error());
  }
  return {};
}

// 3. Switch to ROM mode using custom Focal command (only for test firmware)
std::expected<void, FtError> SwitchToRom(UsbDevice& device) {
  auto data = SendEcCommandAndReadResponse(device, kTestEcSwitchToRomCmd, 0,
                                           kSwitchToRomPayload);
  if (!data) {
    return std::unexpected(data.error());
  }
  return {};
}

// ============================================================================
// CheckEcBuildVersion - with retries
// ============================================================================
std::expected<void, FtError> CheckEcBuildVersion(UsbDevice& usb,
                                                 FtDeviceMode current_mode) {
  if (current_mode != FtDeviceMode::kTest &&
      current_mode != FtDeviceMode::kEc) {
    FT_LOGE("Invalid mode for fw version get");
    return std::unexpected(FtError::kResultFail);
  }

  constexpr int kRetries = kMaxRetry;
  std::string build_info;

  for (int attempt = 1; attempt <= kRetries; ++attempt) {
    auto result = GetBuildInfo(usb);
    if (result) {
      build_info = std::move(*result);
      break;
    }
    FT_LOGW(
        "GetBuildInfo failed (attempt {}): {}", attempt,
        (result.error() == FtError::kNoSuchDevice ? "device lost" : "error"));
    std::this_thread::sleep_for(kMcuRebootPollInterval);
  }

  if (build_info.empty()) {
    FT_LOGE("Failed to retrieve EC build version after {} retries", kRetries);
    return std::unexpected(FtError::kResultFail);
  }

  FT_LOGI("EC build version: {}", build_info);

  bool is_focal_device = std::ranges::any_of(
      kSupportedProjects, [&build_info](std::string_view project) {
        return build_info.contains(project);
      });

  if (!is_focal_device) {
    FT_LOGE("Can't find focal project device");
    return std::unexpected(FtError::kNoSuchDevice);
  }
  return {};
}

// ============================================================================
// SwitchMcuMode - generic mode switch with device-loss detection
// ============================================================================
std::expected<void, FtError> SwitchMcuMode(UsbDevice& usb,
                                           FtDeviceMode current_mode,
                                           Command target_cmd) {
  // First, verify EC build version (requires proper communication)
  if (auto res = CheckEcBuildVersion(usb, current_mode); !res) {
    FT_LOGE("Cannot get EC build version before switch");
    return std::unexpected(FtError::kResultFail);
  }

  // Choose the appropriate switch function
  auto switch_func = [&]() -> std::expected<void, FtError> {
    if (target_cmd == Command::kEnterBoot) {
      return SwitchToBootloader(usb, kEcBootloaderModeDefault);
    } else if (target_cmd == Command::kEnterRom) {
      return SwitchToRom(usb);
    } else {
      FT_LOGE("Invalid target command for mode switch");
      return std::unexpected(FtError::kParameterError);
    }
  };

  // Attempt the switch with retries; device loss indicates success
  constexpr int kMaxSwitchRetries = kMaxRetry;
  bool device_lost = false;

  for (int attempt = 1; attempt <= kMaxSwitchRetries; ++attempt) {
    auto result = switch_func();
    if (!result) {
      if (result.error() == FtError::kNoSuchDevice) {
        FT_LOGW("USB device lost (expected during {} switch).",
                target_cmd == Command::kEnterBoot ? "bootloader" : "ROM");
        device_lost = true;
        break;
      }
      FT_LOGW("Switch to {} failed (attempt {}): error={}",
              target_cmd == Command::kEnterBoot ? "bootloader" : "ROM", attempt,
              std::to_underlying(result.error()));
      std::this_thread::sleep_for(kMcuRebootPollInterval);
      continue;
    }
    // Command succeeded without device loss (rare, but possible)
    FT_LOGI("Switch to {} command sent successfully",
            target_cmd == Command::kEnterBoot ? "bootloader" : "ROM");
    device_lost = true;  // We still expect a reset, so wait for re-enumeration
    break;
  }

  if (!device_lost) {
    FT_LOGE("Failed to send switch command after {} attempts",
            kMaxSwitchRetries);
    return std::unexpected(FtError::kResultFail);
  }

  // Wait for the device to reappear after reset
  if (auto wait_res = usb.WaitForReset(kResetWaitTimeout); !wait_res) {
    FT_LOGE("Device did not reappear after mode switch");
    return std::unexpected(FtError::kResultFail);
  }
  return {};
}

// ============================================================================
// Command parsing and execution
// ============================================================================
constexpr Command ParseCommand(std::string_view cmd_str) {
  if (cmd_str == "enter_bootloader") return Command::kEnterBoot;
  if (cmd_str == "enter_rom") return Command::kEnterRom;
  if (cmd_str == "flash_get_protect") return Command::kGetFlashWp;
  if (cmd_str == "flash_set_protect") return Command::kSetFlashWp;
  if (cmd_str == "get_ec_version") return Command::kGetEcVersion;
  if (cmd_str.ends_with(".bin")) return Command::kUpdate;
  return Command::kUnknown;
}

std::expected<int, FtError> ParseInt(std::string_view str) {
  int value = 0;
  auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
  if (ec != std::errc() || ptr != str.data() + str.size()) {
    return std::unexpected(FtError::kParameterError);
  }
  return value;
}

std::expected<AppConfig, FtError> ParseArguments(
    std::span<const std::string_view> args) {
  if (args.empty()) {
    FT_LOGE("No subcommand specified.");
    return std::unexpected(FtError::kParameterError);
  }

  AppConfig config{
      .cmd = ParseCommand(args[0]),
  };

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

std::expected<void, FtError> ExecuteCommand(const AppConfig& config,
                                            UsbDevice& usb_device,
                                            FtDeviceInfo& info) {
  switch (config.cmd) {
    case Command::kGetEcVersion:
      FT_LOGI("Tool version is {}", kToolVersion);
      return CheckEcBuildVersion(usb_device, info.current_mode);

    case Command::kSetFlashWp:
      return FtSetFlashProtect(usb_device, FtFlashWpArea{config.action_param},
                               info.current_mode);

    case Command::kGetFlashWp: {
      auto wp_res = FtGetFlashProtect(usb_device, info.current_mode);
      if (!wp_res) {
        return std::unexpected(wp_res.error());
      }
      FT_LOGI("Flash write protect area = {}", std::to_underlying(*wp_res));
      return {};
    }

    case Command::kEnterBoot:
      FT_LOGI("Entering bootloader mode...");
      return SwitchMcuMode(usb_device, info.current_mode, Command::kEnterBoot);

    case Command::kEnterRom:
      FT_LOGI("Entering ROM mode...");
      if (info.current_mode == FtDeviceMode::kTest) {
        return SwitchMcuMode(usb_device, info.current_mode, Command::kEnterRom);
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
