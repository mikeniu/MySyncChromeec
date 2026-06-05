/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_CLI_H_
#define UTIL_FOCALTECH_CLI_H_

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <numeric>
#include <print>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include "ft_log.h"
#include "ft_util.h"
#include "libusb_transport.h"
#include "usb_device.h"

namespace focaltech {

class UsbDevice;

inline constexpr std::array<std::string_view, 4> kSupportedProjects = {
    "ft9001", "chojnik", "chudow", "chobienia"};
inline constexpr int kMaxRetry = 5;
constexpr uint8_t kEcBootloaderModeDefault = 0;

enum class Command {
  kUnknown,
  kEnterBoot,
  kEnterRom,
  kGetFlashWp,
  kSetFlashWp,
  kGetEcVersion,
  kUpdate,
};

struct AppConfig {
  Command cmd = Command::kUnknown;
  std::string_view ec_bin;
  uint32_t action_param = 0;
};

std::expected<AppConfig, Error> ParseArguments(
    std::span<const std::string_view> args);

std::expected<void, Error> ExecuteCommand(const AppConfig& config,
                                          UsbDevice& usb_device,
                                          DeviceMode current_mode);

std::expected<std::string, Error> GetBuildInfo(UsbDevice& device);

std::expected<void, Error> CheckEcBuildVersion(UsbDevice& usb,
                                               DeviceMode current_mode);

std::expected<void, Error> SwitchToBootloader(
    UsbDevice& device, uint8_t mode = kEcBootloaderModeDefault);

std::expected<void, Error> SwitchToRom(UsbDevice& device);

std::expected<void, Error> SwitchMcuMode(UsbDevice& usb,
                                         DeviceMode current_mode,
                                         Command target_cmd);

std::expected<int, Error> ParseInt(std::string_view str);

namespace internal {
std::expected<std::vector<uint8_t>, Error> SendEcCommandAndReadResponse(
    UsbDevice& device, uint16_t command, uint8_t version,
    std::span<const uint8_t> params = {});
}  // namespace internal

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_CLI_H_
