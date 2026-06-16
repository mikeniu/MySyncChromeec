/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cli.h"

#include <ec_commands.h>

#include <utility>

#include "updater.h"

namespace focaltech {

namespace {

using namespace std::chrono_literals;
constexpr auto kMcuRebootPollInterval = 10ms;
constexpr auto kEcCommandTimeout = 200ms;

constexpr uint16_t kTestEcSwitchToRomCmd = 0xFACE;
constexpr std::array<uint8_t, 3> kSwitchToRomPayload = {0xAA, 0x06, 0x00};

// Parse a string to a FlashWpArea.
std::expected<FlashWpArea, Error> ParseWpArea(std::string_view arg) {
  if (arg == "none") return FlashWpArea::kNone;
  if (arg == "boot") return FlashWpArea::kBoot;
  if (arg == "ro") return FlashWpArea::kRo;
  if (arg == "all") return FlashWpArea::kAll;
  return std::unexpected(Error::kParameterError);
}

std::vector<uint8_t> BuildEcCommand(uint16_t command, uint8_t version,
                                    std::span<const uint8_t> params = {}) {
  ec_host_request req{
      .struct_version = EC_HOST_REQUEST_VERSION,
      .checksum = 0,
      .command = ToLittleEndian(command),
      .command_version = version,
      .reserved = 0,
      .data_len = ToLittleEndian(static_cast<uint16_t>(params.size())),
  };

  std::vector<uint8_t> packet;
  packet.reserve(sizeof(req) + params.size());

  auto req_span = AsUint8Span(req);
  packet.insert(packet.end(), req_span.begin(), req_span.end());
  packet.insert(packet.end(), params.begin(), params.end());

  uint8_t checksum = std::accumulate(packet.begin(), packet.end(), uint8_t{0});
  packet[offsetof(ec_host_request, checksum)] = -checksum;
  return packet;
}

}  // namespace

namespace internal {

std::expected<std::vector<uint8_t>, Error> SendEcCommandAndReadResponse(
    UsbDevice& device, uint16_t command, uint8_t version,
    std::span<const uint8_t> params) {
  auto packet = BuildEcCommand(command, version, params);

  auto send_result = device.Send(packet, kEcCommandTimeout);
  if (!send_result) {
    if (send_result.error() == Error::kNoSuchDevice) {
      return std::unexpected(Error::kNoSuchDevice);
    }
    return std::unexpected(send_result.error());
  }

  std::array<uint8_t, kUsbBulkMaxPacketSize> initial_buffer;
  auto recv_initial = device.Recv(initial_buffer, kEcCommandTimeout);
  if (!recv_initial) {
    if (recv_initial.error() == Error::kNoSuchDevice) {
      return std::unexpected(Error::kNoSuchDevice);
    }
    return std::unexpected(Error::kResultFail);
  }

  size_t received = *recv_initial;
  if (received < sizeof(ec_host_response)) {
    FT_LOGE("Insufficient data for EC response header: got {} bytes, need {}",
            received, sizeof(ec_host_response));
    return std::unexpected(Error::kResultFail);
  }

  ec_host_response resp;
  memcpy(&resp, initial_buffer.data(), sizeof(resp));
  uint16_t data_len = FromLittleEndian(resp.data_len);
  FT_LOGD("resp.data_len = {}", data_len);

  std::vector<uint8_t> response_data(
      initial_buffer.begin() + sizeof(ec_host_response),
      initial_buffer.begin() + received);

  if (response_data.size() < data_len) {
    size_t remaining = data_len - response_data.size();
    std::vector<uint8_t> additional(remaining);
    auto recv_more = device.Recv(additional, kEcCommandTimeout);
    if (!recv_more || std::cmp_not_equal(*recv_more, remaining)) {
      FT_LOGE(
          "Failed to read remaining response data: expected {} bytes, got {}",
          remaining, recv_more.has_value() ? *recv_more : -1);
      return std::unexpected(Error::kResultFail);
    }
    response_data.insert(response_data.end(), additional.begin(),
                         additional.end());
  }

  if (response_data.size() > data_len) {
    response_data.resize(data_len);
  }

  return response_data;
}

}  // namespace internal

std::expected<std::string, Error> GetBuildInfo(UsbDevice& device) {
  auto data = internal::SendEcCommandAndReadResponse(
      device, EC_CMD_GET_BUILD_INFO, kEcCmdVersion0, {});
  if (!data) {
    return std::unexpected(data.error());
  }
  auto end_it = std::ranges::find(*data, '\0');
  return std::string(data->begin(), end_it);
}

std::expected<void, Error> SwitchToBootloader(UsbDevice& device, uint8_t mode) {
  ec_params_enter_bootloader params{
      .mode = mode,
  };
  auto params_span = AsUint8Span(params);
  auto data = internal::SendEcCommandAndReadResponse(
      device, EC_CMD_ENTER_BOOTLOADER, kEcCmdVersion0, params_span);
  if (!data) {
    return std::unexpected(data.error());
  }
  return {};
}

std::expected<void, Error> SwitchToRom(UsbDevice& device) {
  auto data = internal::SendEcCommandAndReadResponse(
      device, kTestEcSwitchToRomCmd, kEcCmdVersion0, kSwitchToRomPayload);
  if (!data) {
    return std::unexpected(data.error());
  }
  return {};
}

std::expected<void, Error> CheckEcBuildVersion(UsbDevice& usb,
                                               DeviceMode current_mode) {
  if (current_mode != DeviceMode::kTest && current_mode != DeviceMode::kEc) {
    auto res = GetBootVersion(usb, current_mode);
    if (!res) {
      FT_LOGE("Get boot version failed");
      return std::unexpected(Error::kResultFail);
    }
    return {};
  }

  std::string build_info;

  for (int attempt = 1; attempt <= kMaxRetry; ++attempt) {
    auto result = GetBuildInfo(usb);
    if (result) {
      build_info = std::move(*result);
      break;
    }
    FT_LOGW("GetBuildInfo failed (attempt {}): {}", attempt,
            (result.error() == Error::kNoSuchDevice ? "device lost" : "error"));
    std::this_thread::sleep_for(kMcuRebootPollInterval);
  }

  if (build_info.empty()) {
    FT_LOGE("Failed to retrieve EC build version after {} retries", kMaxRetry);
    return std::unexpected(Error::kResultFail);
  }

  FT_LOGI("EC build version: {}", build_info);

  bool is_focal_device = std::ranges::any_of(
      kSupportedProjects, [&build_info](std::string_view project) {
        return build_info.contains(project);
      });

  if (!is_focal_device) {
    FT_LOGE("Can't find focal project device");
    return std::unexpected(Error::kNoSuchDevice);
  }
  return {};
}

std::expected<void, Error> SwitchMcuMode(UsbDevice& usb,
                                         DeviceMode current_mode,
                                         Command target_cmd) {
  if (auto res = CheckEcBuildVersion(usb, current_mode); !res) {
    FT_LOGE("Cannot get EC build version before switch");
    return std::unexpected(Error::kResultFail);
  }

  auto switch_func = [&]() -> std::expected<void, Error> {
    if (target_cmd == Command::kEnterBoot) {
      return SwitchToBootloader(usb, kEcBootloaderModeDefault);
    } else if (target_cmd == Command::kEnterRom) {
      return SwitchToRom(usb);
    } else {
      FT_LOGE("Invalid target command for mode switch");
      return std::unexpected(Error::kParameterError);
    }
  };

  std::string_view target_name =
      (target_cmd == Command::kEnterBoot) ? "bootloader" : "ROM";

  for (int attempt = 1; attempt <= kMaxRetry; ++attempt) {
    auto result = switch_func();
    if (result) {
      FT_LOGI("Switch to {} command sent successfully", target_name);
      break;
    }
    if (result.error() == Error::kNoSuchDevice) {
      FT_LOGW("USB device lost (expected during {} switch).", target_name);
      break;
    }
    if (attempt == kMaxRetry) {
      FT_LOGE("Failed to send switch command after {} attempts", kMaxRetry);
      return std::unexpected(Error::kResultFail);
    }
    FT_LOGW("Switch to {} failed (attempt {}): error={}", target_name, attempt,
            ToString(result.error()));
    std::this_thread::sleep_for(kMcuRebootPollInterval);
  }

  return usb.WaitForReset(kDeviceRebootTimeout);
}

constexpr Command ParseCommand(std::string_view cmd_str) {
  if (cmd_str == kCmdEnterBoot) return Command::kEnterBoot;
  if (cmd_str == kCmdEnterRom) return Command::kEnterRom;
  if (cmd_str == kCmdGetFlashWp) return Command::kGetFlashWp;
  if (cmd_str == kCmdSetFlashWp) return Command::kSetFlashWp;
  if (cmd_str == kCmdGetEcVersion) return Command::kGetEcVersion;
  if (cmd_str.ends_with(".bin")) return Command::kUpdate;
  return Command::kUnknown;
}

std::expected<int, Error> ParseInt(std::string_view str) {
  int value = 0;
  auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
  if (ec != std::errc() || ptr != str.data() + str.size()) {
    return std::unexpected(Error::kParameterError);
  }
  return value;
}

std::expected<AppConfig, Error> ParseArguments(
    std::span<const std::string_view> args) {
  if (args.empty()) {
    FT_LOGE("No subcommand specified.");
    return std::unexpected(Error::kParameterError);
  }

  AppConfig config{
      .cmd = ParseCommand(args[0]),
  };

  if (config.cmd == Command::kUnknown) {
    FT_LOGE("Unknown subcommand: {}", args[0]);
    return std::unexpected(Error::kParameterError);
  }

  if (config.cmd == Command::kUpdate) {
    if (args.size() < 1) {
      FT_LOGE("Update command requires firmware file (.bin)");
      return std::unexpected(Error::kParameterError);
    }
    config.ec_bin = args[0];
  } else if (config.cmd == Command::kSetFlashWp) {
    if (args.size() < 2) {
      FT_LOGE(
          "flash_set_protect requires an area parameter (none, boot, ro, "
          "all).");
      return std::unexpected(Error::kParameterError);
    }
    auto wp_res = ParseWpArea(args[1]);
    if (!wp_res) {
      FT_LOGE("Invalid protection area: '{}'. Must be none, boot, ro, or all.",
              args[1]);
      return std::unexpected(Error::kParameterError);
    }
    config.wp_area = *wp_res;
  }
  return config;
}

std::expected<void, Error> ExecuteCommand(const AppConfig& config,
                                          UsbDevice& usb_device,
                                          DeviceMode current_mode) {
  switch (config.cmd) {
    case Command::kGetEcVersion:
      return CheckEcBuildVersion(usb_device, current_mode);

    case Command::kSetFlashWp:
      return SetFlashProtection(usb_device, config.wp_area, current_mode);

    case Command::kGetFlashWp: {
      auto wp_res = GetFlashProtection(usb_device, current_mode);
      if (!wp_res) {
        return std::unexpected(wp_res.error());
      }
      FT_LOGI("Flash write protect area = {}", ToString(*wp_res));
      return {};
    }

    case Command::kEnterBoot:
      // enter_bootloader is only valid from EC mode (PID 0x5403 or 0x5404).
      // If the device is already in bootloader or ROM mode, the EC command
      // will not be understood and will cause a confusing timeout.
      if (current_mode != DeviceMode::kEc) {
        FT_LOGE(
            "enter_bootloader command is only valid from EC mode. "
            "Current mode: {}",
            static_cast<int>(current_mode));
        return std::unexpected(Error::kInvalidMode);
      }
      FT_LOGI("Entering bootloader mode...");
      return SwitchMcuMode(usb_device, current_mode, Command::kEnterBoot);

    case Command::kEnterRom:
      FT_LOGI("Entering ROM mode...");
      if (current_mode == DeviceMode::kTest) {
        return SwitchMcuMode(usb_device, current_mode, Command::kEnterRom);
      }
      // From bootloader mode, erase the config page and reset.
      return ReturnToRomBoot(usb_device, current_mode);

    case Command::kUpdate: {
      FT_LOGI("Updating firmware...");
      if (config.ec_bin.empty()) {
        FT_LOGE("No firmware file specified");
        return std::unexpected(Error::kParameterError);
      }

      auto fw_data_res = ReadFileToVector(config.ec_bin);
      if (!fw_data_res) {
        return std::unexpected(fw_data_res.error());
      }

      /* Fix an issue where, when the display is off but the system has not yet
       * entered suspend, the host may forcibly switch the FPMCU into suspend
       * state, causing the upgrade to fail. Retry updating the firmware. */
      for (int attempt = 1; attempt <= kMaxRetry; ++attempt) {
        if (auto res = UpdateFw(usb_device, *fw_data_res, current_mode); res) {
          FT_LOGI("Firmware update successful");
          return {};
        }
        FT_LOGW("Firmware update fail attempt {}", attempt);
      }
      FT_LOGE("Firmware update failed");
      return std::unexpected(Error::kResultFail);
    }

    default:
      FT_LOGE("Command not implemented.");
      return std::unexpected(Error::kParameterError);
  }
}

}  // namespace focaltech
