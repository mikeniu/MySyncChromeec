#include <array>
#include <chrono>
#include <cstdlib>
#include <print>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include "ft_help.h"
#include "ft_log.h"
#include "usb_context.h"

// Forward declarations
std::expected<void, FtError> UpdateFw(const UsbDevice& device,
                                      FtDeviceInfo& info);
std::expected<void, FtError> FtSetFlashProtect(const UsbDevice& device,
                                               FtFlashWpArea wp_area);
std::expected<void, FtError> FtBackToRomBoot(const UsbDevice& device);
std::expected<FtFlashWpArea, FtError> FtGetFlashProtect(
    const UsbDevice& device);

// Project name constants
constexpr std::string_view kFtProjectNameTest = "ft9001";
constexpr std::string_view kFtProjectName9865 = "chojnik";
constexpr std::string_view kFtProjectName9869 = "chudow";
constexpr std::string_view kFtProjectName9849 = "chobienia";

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
  int action_param = 0;
};

// Commands sent to the EC‑mode device via direct bulk transfer.
constexpr std::array<uint8_t, 8> kTestEcSwitchToRomCmd = {
    0x03, 0x00, 0xCE, 0xFA, 0x00, 0xAA, 0x06, 0x00};
constexpr std::array<uint8_t, 8> kSwitchToBootCmd = {0x03, 0x1B, 0xE2, 0x00,
                                                     0x00, 0x00, 0x00, 0x00};
constexpr std::array<uint8_t, 8> kGetBuildVersionCmd = {0x03, 0xF9, 0x04, 0x00,
                                                        0x00, 0x00, 0x00, 0x00};

constexpr Command ParseCommand(std::string_view cmd_str) {
  if (cmd_str == "back_to_bootloader") return Command::kEnterBoot;
  if (cmd_str == "back_to_rom_boot") return Command::kEnterRom;
  if (cmd_str == "flash_get_protect") return Command::kGetFlashWp;
  if (cmd_str == "flash_set_protect") return Command::kSetFlashWp;
  if (cmd_str == "get_ec_version") return Command::kGetEcVersion;
  if (cmd_str.ends_with(".bin")) return Command::kUpdate;

  return Command::kUnknown;
}

// Queries the EC build version string and checks that it's a Focal device.
std::expected<void, FtError> CheckEcBuildVersion(UsbDevice& usb,
                                                 FtDeviceMode current_mode) {
  constexpr size_t kRxLen = 64;
  std::array<uint8_t, kRxLen> rx{};
  constexpr int kRetries = 3;

  if (current_mode != FtDeviceMode::kTest &&
      current_mode != FtDeviceMode::kEc && current_mode != FtDeviceMode::kRo &&
      current_mode != FtDeviceMode::kRw) {
    LOGE("Invalid mode for fw version get");
    return std::unexpected(FtError::kResultFail);
  }

  for (int i = 0; i < kRetries; ++i) {
    auto sr = usb.Send(kGetBuildVersionCmd, 200);
    if (!sr.has_value() || sr.value() <= 0) {
      LOGW("Send build-version cmd failed.");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    auto rr = usb.Recv(rx, 200);
    if (rr.has_value() && rr.value() > 0) {
      break;
    }
    LOGW("Recv build-version failed.");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Version string starts at byte 8.
  std::string_view info(reinterpret_cast<const char*>(rx.data() + 8),
                        kRxLen - 9);
  LOGI("EC build version: {}", info);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  if (info.find(kFtProjectNameTest) == std::string_view::npos &&
      info.find(kFtProjectName9865) == std::string_view::npos &&
      info.find(kFtProjectName9869) == std::string_view::npos &&
      info.find(kFtProjectName9849) == std::string_view::npos) {
    LOGE("can't find focal project device");
    return std::unexpected(FtError::kNoSuchDevice);
  }
  return {};
}

// Sends the "switch to ROM mode" command over direct bulk USB.
std::expected<void, FtError> TestEcSwitchToRom(UsbDevice& usb,
                                               FtDeviceMode current_mode) {
  std::array<uint8_t, 4> rx{};
  constexpr int kRetries = 3;

  if (auto res = CheckEcBuildVersion(usb, current_mode); !res) {
    LOGE("can't find focal device");
    return std::unexpected(FtError::kResultFail);
  }

  for (int i = 0; i < kRetries; ++i) {
    auto sr = usb.Send(kTestEcSwitchToRomCmd, 200);
    if (!sr.has_value()) {
      LOGW("MCU reset to ROM — send failed (expected).");
    }

    auto rr = usb.Recv(rx, 200);
    if (rr.has_value() && rr.value() > 0) {
      LOGW("MCU reset — received {} bytes.", rr.value());
    }
    if (!rr.has_value()) {
      LOGW("MCU can't receive data during reset.");
    } else {
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (usb.ResetComplete(3)) {  // renamed method
    return std::unexpected(FtError::kResultFail);
  }

  return {};
}

// Sends the "switch to bootloader" command over direct bulk USB.
std::expected<void, FtError> SwitchToBoot(UsbDevice& usb,
                                          FtDeviceMode current_mode) {
  if (current_mode == FtDeviceMode::kBoot) {
    LOGW("Already in bootloader mode.");
    return {};
  }
  if (current_mode != FtDeviceMode::kTest &&
      current_mode != FtDeviceMode::kEc && current_mode != FtDeviceMode::kRo &&
      current_mode != FtDeviceMode::kRw) {
    LOGE("Can only switch from EC mode to boot mode.");
    return std::unexpected(FtError::kResultFail);
  }

  if (auto res = CheckEcBuildVersion(usb, current_mode); !res) {
    LOGE("can't find focal device");
    return std::unexpected(FtError::kResultFail);
  }

  std::array<uint8_t, 4> rx{};
  constexpr int kRetries = 3;

  for (int i = 0; i < kRetries; ++i) {
    auto sr = usb.Send(kSwitchToBootCmd, 200);
    if (!sr.has_value()) {
      LOGW("MCU reset to boot — send failed (expected).");
    }

    auto rr = usb.Recv(rx, 200);
    if (rr.has_value() && rr.value() > 0) {
      LOGW("MCU reset — received {} bytes.", rr.value());
    }
    if (!rr.has_value()) {
      LOGW("Can't receive data during MCU reset.");
    } else {
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (usb.ResetComplete(3)) {  // renamed method
    return std::unexpected(FtError::kResultFail);
  }

  return {};
}

std::expected<AppConfig, FtError> ParseArguments(
    std::span<const std::string_view> args) {
  AppConfig config;

  if (args.empty()) {
    LOGE("No subcommand specified.");
    return std::unexpected(FtError::kParameterError);
  }

  config.cmd = ParseCommand(args[0]);

  if (config.cmd == Command::kUnknown) {
    LOGE("Unknown subcommand: {}", args[0]);
    return std::unexpected(FtError::kParameterError);
  }

  if (config.cmd == Command::kUpdate) {
    if (args.size() < 1) {
      LOGE("Update command requires firmware file (.bin)");
      return std::unexpected(FtError::kParameterError);
    }
    config.ec_bin = args[0];
  } else if (config.cmd == Command::kSetFlashWp) {
    if (args.size() < 2) {
      LOGE("flash_set_protect requires an area parameter (1-4).");
      return std::unexpected(FtError::kParameterError);
    }
    config.action_param = std::atoi(args[1].data());
  }

  return config;
}

// Initializes device info based on USB descriptor to determine current mode.
std::expected<void, FtError> DeviceInit(const UsbConnectionDetails& details,
                                        FtDeviceInfo& info) {
  info.action = FtDeviceAction::kNormal;

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
    LOGE("Unrecognized Focal device.");
    return std::unexpected(FtError::kNoSuchDevice);
  }
  return {};
}

int main(int argc, char** argv) {
  std::vector<std::string_view> args(argv + 1, argv + argc);
  int retry = 3, cnt = 0;

  if (!args.empty() && (args[0] == "-h" || args[0] == "--help")) {
    std::println(ft_help::kCmdHelpString);
    return EXIT_SUCCESS;
  }

  auto config_res = ParseArguments(args);
  if (!config_res.has_value()) {
    return EXIT_FAILURE;
  }
  const AppConfig& config = config_res.value();

  UsbContext usb_ctx;
  if (!usb_ctx.IsValid()) {
    LOGE("Failed to initialize libusb.");
    return EXIT_FAILURE;
  }

  UsbConnectionDetails details;
  auto device_result = UsbDevice::Create(usb_ctx, details);
  if (!device_result) {
    LOGE("Failed to open device: error code {}",
         static_cast<int>(device_result.error()));
    return EXIT_FAILURE;
  }

  UsbDevice usb_device = std::move(*device_result);

  FtDeviceInfo info = {};
  auto init_res = DeviceInit(details, info);
  if (!init_res) {
    LOGE("Cannot find Focal device");
    return EXIT_FAILURE;
  }

  switch (config.cmd) {
    case Command::kGetEcVersion:
      LOGI("Tool version is v1.0.2");
      if (auto res = CheckEcBuildVersion(usb_device, info.current_mode); !res) {
        LOGE("Failed to get EC version");
        return EXIT_FAILURE;
      }
      break;

    case Command::kSetFlashWp:
      LOGI("Setting flash write protect to area: {}", config.action_param);
      if (info.current_mode != FtDeviceMode::kBoot) {
        LOGE("Set flash protect should only be done in boot mode");
        return EXIT_FAILURE;
      }
      if (config.action_param < 1 || config.action_param > 4) {
        LOGE("Invalid wp area parameter: {}. Use 1-4.", config.action_param);
        return EXIT_FAILURE;
      }
      if (auto res = FtSetFlashProtect(
              usb_device, static_cast<FtFlashWpArea>(config.action_param));
          !res) {
        LOGE("Failed to set flash protect");
        return EXIT_FAILURE;
      }
      break;

    case Command::kGetFlashWp:
      LOGI("Getting flash write protect area...");
      if (info.current_mode != FtDeviceMode::kBoot) {
        LOGE("Get flash protect should only be done in boot mode");
        return EXIT_FAILURE;
      }
      if (auto res = FtGetFlashProtect(usb_device); res) {
        LOGI("Flash write protect area = {}", static_cast<int>(*res));
      } else {
        LOGE("Failed to get flash protect");
        return EXIT_FAILURE;
      }
      break;

    case Command::kEnterBoot:
      LOGI("Switching to bootloader mode...");
      if (info.current_mode != FtDeviceMode::kTest &&
          info.current_mode != FtDeviceMode::kEc &&
          info.current_mode != FtDeviceMode::kRo &&
          info.current_mode != FtDeviceMode::kRw) {
        LOGE("Current mode cannot switch to bootloader");
        return EXIT_FAILURE;
      }
      if (auto res = SwitchToBoot(usb_device, info.current_mode); !res) {
        LOGE("Failed to switch to bootloader");
        return EXIT_FAILURE;
      }
      break;

    case Command::kEnterRom:
      LOGI("Switching to ROM mode...");
      if (info.current_mode == FtDeviceMode::kTest) {
        if (auto res = TestEcSwitchToRom(usb_device, info.current_mode); !res) {
          LOGE("Failed to switch to ROM via test command");
          return EXIT_FAILURE;
        }
      } else if (info.current_mode == FtDeviceMode::kBoot) {
        if (auto res = FtBackToRomBoot(usb_device); !res) {
          LOGE("Failed to switch to ROM via back command");
          return EXIT_FAILURE;
        }
      } else {
        LOGE("Current mode cannot switch to ROM");
        return EXIT_FAILURE;
      }
      break;

    case Command::kUpdate:
      LOGI("Updating firmware...");
      if (config.ec_bin.empty()) {
        LOGE("No firmware file specified");
        return EXIT_FAILURE;
      }

      if (info.current_mode != FtDeviceMode::kBoot &&
          info.current_mode != FtDeviceMode::kRom) {
        LOGE("Invalid mode for FW update");
        return EXIT_FAILURE;
      }

      info.ec_bin = config.ec_bin;

      while (retry--) {
        cnt++;
        if (auto res = UpdateFw(usb_device, info); !res) {
          LOGW("Firmware update fail cnt {}", cnt);
          continue;
        } else {
          LOGI("Firmware update successful");
          return EXIT_SUCCESS;
        }
      }
      LOGE("Firmware update failed");
      return EXIT_FAILURE;

    default:
      LOGE("Command not implemented.");
      return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}