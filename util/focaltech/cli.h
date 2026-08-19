/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_CLI_H_
#define UTIL_FOCALTECH_CLI_H_

#include <algorithm>
#include <array>
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
#include "updater.h"
#include "usb_device.h"

namespace focaltech {

class UsbDevice;

inline constexpr int kMaxRetry = 5;

inline constexpr std::string_view kCmdEnterRom = "enter_rom";
inline constexpr std::string_view kCmdGetFlashWp = "flash_get_protect";
inline constexpr std::string_view kCmdSetFlashWp = "flash_set_protect";

enum class Command {
  kUnknown,
  kEnterRom,
  kGetFlashWp,
  kSetFlashWp,
  kUpdate,
};

struct AppConfig {
  Command cmd = Command::kUnknown;
  std::string_view firmware_path;
  FlashWpArea wp_area = FlashWpArea::kUnknown;
};

std::expected<AppConfig, Error> ParseArguments(
    std::span<const std::string_view> args);

std::expected<void, Error> ExecuteCommand(const AppConfig& config,
                                          UsbDevice& usb_device,
                                          DeviceMode current_mode);

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_CLI_H_
