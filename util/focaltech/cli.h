#ifndef UTIL_FOCALTECH_CLI_H_
#define UTIL_FOCALTECH_CLI_H_

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <print>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include "ft_log.h"
#include "ft_util.h"
#include "libusb_transport.h"
#include "usb_device.h"

class UsbDevice;

// Constants
inline constexpr std::array<std::string_view, 4> kSupportedProjects = {
    "ft9001", "chojnik", "chudow", "chobienia"};
inline constexpr int kMaxRetry = 5;

// Command enum
enum class Command {
  kUnknown,
  kEnterBoot,
  kEnterRom,
  kGetFlashWp,
  kSetFlashWp,
  kGetEcVersion,
  kUpdate,
};

// App configuration struct
struct AppConfig {
  Command cmd = Command::kUnknown;
  std::string_view ec_bin;
  int action_param = 0;
};

// Function declarations
std::expected<AppConfig, FtError> ParseArguments(
    std::span<const std::string_view> args);

std::expected<void, FtError> DeviceInit(const UsbDeviceId& details,
                                        FtDeviceInfo& info);

std::expected<void, FtError> ExecuteCommand(const AppConfig& config,
                                            UsbDevice& usb_device,
                                            FtDeviceInfo& info);

std::expected<void, FtError> CheckEcBuildVersion(UsbDevice& usb,
                                                 FtDeviceMode current_mode);

std::expected<void, FtError> SwitchMcuMode(UsbDevice& usb,
                                           FtDeviceMode current_mode,
                                           std::span<const uint8_t> switch_cmd,
                                           std::string_view mode_name);

std::expected<int, FtError> ParseInt(std::string_view str);

#endif  // UTIL_FOCALTECH_CLI_H_
