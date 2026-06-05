/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "updater.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

#include "ft_log.h"
#include "ft_scsi.h"
#include "usb_device.h"

namespace focaltech {

namespace {

using namespace std::chrono_literals;

constexpr uint8_t kSoftPor = 0;

// Before bootloader v1.8, erasing the flash takes about 4.4 seconds.
constexpr auto kErasePageTimeout = 6000ms;
constexpr auto kDownloadPageTimeout = 1000ms;
constexpr auto kVerifyHashTimeout = 1000ms;
constexpr auto kGenericInfoTimeout = 200ms;

constexpr uint8_t kHashModeSha256 = 0x04;
constexpr uint8_t kHashModeRomShift = 4;

std::expected<void, Error> ErasePage(const UsbDevice& device, size_t length,
                                     uint32_t addr) {
  uint32_t pages = static_cast<uint32_t>(DivRoundUp(length, kFocalConfigSize));
  return SendBulkDataOut(device,
                         CmdFormat{
                             .opcode = Opcode::kAction,
                             .subcmd = Subcmd::kErasePage,
                             .address = addr,
                             .length = pages,
                         },
                         {}, TransferDirection::kOut, kErasePageTimeout);
}

std::expected<void, Error> DownloadPage(const UsbDevice& device,
                                        uint32_t downaddr, size_t per_len,
                                        std::span<const uint8_t> data) {
  auto res = SendBulkDataOut(device,
                             CmdFormat{
                                 .opcode = Opcode::kAction,
                                 .subcmd = Subcmd::kWriteBin,
                                 .address = kWriteSramAddr,
                                 .length = static_cast<uint32_t>(per_len),
                             },
                             data.subspan(0, per_len), TransferDirection::kOut,
                             kDownloadPageTimeout);
  if (!res) {
    FT_LOGE("Write bin error");
    return res;
  }

  res = SendBulkDataOut(device,
                        CmdFormat{
                            .opcode = Opcode::kAction,
                            .subcmd = Subcmd::kCodeBin,
                            .address = downaddr,
                            .length = static_cast<uint32_t>(per_len),
                            .aux_address = kWriteSramAddr,
                        },
                        {}, TransferDirection::kOut, kDownloadPageTimeout);
  if (!res) FT_LOGE("Code bin error");
  return res;
}

std::expected<void, Error> DownloadBin(const UsbDevice& device,
                                       std::span<const uint8_t> buffer,
                                       uint32_t target_addr) {
  const bool is_config_update =
      (target_addr == kBootCfgAddr || target_addr == kRomCfgAddr);

  size_t file_len = buffer.size();

  if (!is_config_update) {
    auto erase_res = ErasePage(device, kFocalConfigSize, kRomCfgAddr);
    if (!erase_res) return erase_res;
  }

  auto erase_res = ErasePage(device, file_len, target_addr);
  if (!erase_res) return erase_res;

  std::span<const uint8_t> remaining_data = buffer;
  uint32_t downaddr = target_addr;

  if (is_config_update) {
    FT_LOGI("Write config page last");
    remaining_data = remaining_data.subspan(
        std::min(remaining_data.size(), kFocalConfigSize));
    downaddr += kFocalConfigSize;
  }

  [[maybe_unused]] size_t frame = 0;
  [[maybe_unused]] size_t total_frame = DivRoundUp(file_len, kFocalConfigSize);
  while (remaining_data.size() >= kFocalConfigSize) {
    auto page_res = DownloadPage(device, downaddr, kFocalConfigSize,
                                 remaining_data.first(kFocalConfigSize));
    if (!page_res) return page_res;

    remaining_data = remaining_data.subspan(kFocalConfigSize);
    downaddr += kFocalConfigSize;
    FT_LOGD("Download frame {}/{}", ++frame, total_frame);
  }

  if (!remaining_data.empty()) {
    std::array<uint8_t, kFocalConfigSize> final_chunk;
    constexpr uint8_t kErasedFlashByte = 0xFF;
    final_chunk.fill(kErasedFlashByte);
    std::ranges::copy(remaining_data, final_chunk.begin());

    auto page_res =
        DownloadPage(device, downaddr, final_chunk.size(), final_chunk);
    if (!page_res) return page_res;
  }

  if (is_config_update) {
    FT_LOGI("Write config page");
    auto page_res = DownloadPage(device, target_addr, kFocalConfigSize,
                                 buffer.first(kFocalConfigSize));
    if (!page_res) return page_res;
    FT_LOGD("Download frame {}/{}", ++frame, total_frame);
  }

  return {};
}

std::expected<void, Error> VerifyData(const UsbDevice& device,
                                      std::span<const uint8_t> firmware_data,
                                      uint32_t target_addr) {
  uint8_t hash_mode = (target_addr == kRomCfgAddr)
                          ? (kHashModeSha256 << kHashModeRomShift)
                          : kHashModeSha256;

  std::array<uint8_t, SHA256_DIGEST_LENGTH> device_hash{};

  auto io_res = ReceiveBulkDataIn(
      device,
      CmdFormat{
          .opcode = Opcode::kAction,
          .subcmd = Subcmd::kVerify,
          .address = target_addr,
          .length = static_cast<uint32_t>(firmware_data.size()),
          .flags = hash_mode,
      },
      device_hash, kVerifyHashTimeout);

  if (!io_res.has_value()) {
    FT_LOGE("Verification command failed over USB.");
    return std::unexpected(io_res.error());
  }

  auto local_hash = CalculateSha256(firmware_data);
  if (local_hash != device_hash) {
    FT_LOGE("Hash mismatch. Local: {:02x}{:02x}... Device: {:02x}{:02x}...",
            local_hash[0], local_hash[1], device_hash[0], device_hash[1]);
    return std::unexpected(Error::kVerifyFailed);
  }

  FT_LOGD("Verification successful.");
  return {};
}

std::expected<bool, Error> HasValidConfigPage(std::span<const uint8_t> fw_data,
                                              uint32_t& code_start_addr) {
  if (fw_data.size() < sizeof(ConfigPage)) {
    FT_LOGE("Firmware data too small for config page.");
    return std::unexpected(Error::kResultFail);
  }

  ConfigPage config{};
  std::memcpy(&config, fw_data.data(), sizeof(config));

  if (config.code_valid_control_word != kCodeValidMagic) {
    FT_LOGE(
        "Invalid magic number in config page (expected 0x{:08x}, got 0x{:08x})",
        kCodeValidMagic, config.code_valid_control_word);
    return std::unexpected(Error::kFormatError);
  }

  code_start_addr = config.code_start_address;
  FT_LOGD("Config page found: start=0x{:x}", code_start_addr);
  return true;
}

std::expected<void, Error> ResetMcu(const UsbDevice& device,
                                    uint32_t target_addr) {
  if (target_addr != kRomCfgAddr && target_addr != kBootCfgAddr) {
    FT_LOGE("Reset address invalid: {:#x}", target_addr);
    return std::unexpected(Error::kParameterError);
  }

  auto res =
      SendBulkDataOut(device,
                      CmdFormat{
                          .opcode = Opcode::kAction,
                          .subcmd = Subcmd::kSoftReset,
                          .flags = kSoftPor,
                      },
                      {}, TransferDirection::kSendOnly, kDeviceRebootTimeout);

  if (!res) {
    return std::unexpected(Error::kResultFail);
  }

  return {};
}

}  // namespace

std::expected<void, Error> UpdateFw(const UsbDevice& device,
                                    std::span<const uint8_t> fw_data,
                                    DeviceMode current_mode) {
  uint32_t target_addr = kRomCfgAddr;
  uint32_t code_start_addr = 0;

  if (fw_data.size() > kEcBinSizeMax) {
    FT_LOGE("Firmware file exceeds maximum allowed size.");
    return std::unexpected(Error::kParameterError);
  }

  auto abort_update = [&](Error err) {
    if (target_addr == kBootCfgAddr || target_addr == kRomCfgAddr) {
      ErasePage(device, kFocalConfigSize, target_addr);
      FT_LOGW("Update aborted. Staying in {} mode",
              target_addr == kBootCfgAddr ? "boot" : "rom");
    }
    return std::unexpected(err);
  };

  auto config_res = HasValidConfigPage(fw_data, code_start_addr);
  if (!config_res) {
    return std::unexpected(config_res.error());
  }

  if (current_mode == DeviceMode::kBoot) {
    target_addr = kBootCfgAddr;
    if (code_start_addr != kBootRunAddr) {
      FT_LOGE("EC binary not for bootloader mode");
      return std::unexpected(Error::kParameterError);
    }
  } else if (current_mode == DeviceMode::kRom) {
    if (code_start_addr == kBootRunAddr) {
      FT_LOGE("EC binary not for ROM mode");
      return std::unexpected(Error::kParameterError);
    }
    if (code_start_addr != kRomRunAddr) {
      FT_LOGE("EC binary start address mismatch");
      return std::unexpected(Error::kParameterError);
    }
  } else {
    FT_LOGE("Invalid mode for update");
    return std::unexpected(Error::kInvalidMode);
  }

  auto res = GetBootVersion(device, current_mode);
  if (!res) {
    FT_LOGE("Get boot version failed");
    return abort_update(res.error());
  }

  FT_LOGI("Begin firmware update");
  res = DownloadBin(device, fw_data, target_addr);
  if (!res) {
    FT_LOGE("Download failed");
    return abort_update(res.error());
  }

  res = VerifyData(device, fw_data, target_addr);
  if (!res) {
    FT_LOGE("Verification failed");
    return abort_update(res.error());
  }

  FT_LOGI("Firmware flashed successfully");

  res = ResetMcu(device, target_addr);
  if (!res) {
    FT_LOGE("MCU reset failed");
    return abort_update(res.error());
  }

  return {};
}

std::expected<FlashWpArea, Error> GetFlashProtection(const UsbDevice& device,
                                                     DeviceMode current_mode) {
  if (current_mode != DeviceMode::kBoot) {
    FT_LOGE("Get flash protect should only be done in boot mode");
    return std::unexpected(Error::kInvalidMode);
  }

  FT_LOGI("Getting flash write protect area...");

  std::array<uint8_t, kFlashWpBufferSize> data{};

  auto res = ReceiveBulkDataIn(device,
                               CmdFormat{
                                   .opcode = Opcode::kAction,
                                   .subcmd = Subcmd::kFlashWpGet,
                               },
                               data, kGenericInfoTimeout);
  if (!res) {
    FT_LOGE("Failed to get flash protect status");
    return std::unexpected(res.error());
  }

  auto wp_area = static_cast<FlashWpArea>(data[0]);
  FT_LOGI("Flash write-protect area = {}", ToString(wp_area));
  return wp_area;
}

std::expected<void, Error> SetFlashProtection(const UsbDevice& device,
                                              FlashWpArea wp_area,
                                              DeviceMode current_mode) {
  if (current_mode != DeviceMode::kBoot) {
    FT_LOGE("Get flash protect should only be done in boot mode");
    return std::unexpected(Error::kInvalidMode);
  }

  if (wp_area <= FlashWpArea::kUnknown || wp_area >= FlashWpArea::kOutRange) {
    FT_LOGE("Invalid wp_area: {}", ToString(wp_area));
    return std::unexpected(Error::kParameterError);
  }

  FT_LOGI("Setting flash write protect to area: {}", ToString(wp_area));

  std::array<uint8_t, kFlashWpBufferSize> ignored_response{};

  auto res = ReceiveBulkDataIn(
      device,
      CmdFormat{
          .opcode = Opcode::kAction,
          .subcmd = Subcmd::kFlashWpSet,
          .address = static_cast<uint32_t>(std::to_underlying(wp_area)),
          .length = static_cast<uint32_t>(~std::to_underlying(wp_area)),
      },
      ignored_response, kGenericInfoTimeout);
  if (!res) {
    FT_LOGE("Failed to set flash protect");
    return std::unexpected(res.error());
  }

  auto get_res = GetFlashProtection(device, current_mode);
  if (!get_res || *get_res != wp_area) {
    FT_LOGE("Flash protect verification failed");
    return std::unexpected(Error::kResultFail);
  }

  FT_LOGI("Flash write-protect successfully set to area {}", ToString(wp_area));
  return {};
}

std::expected<void, Error> ReturnToRomBoot(const UsbDevice& device,
                                           DeviceMode current_mode) {
  if (current_mode != DeviceMode::kBoot) {
    FT_LOGE("Current mode cannot switch to ROM via back command");
    return std::unexpected(Error::kInvalidMode);
  }
  auto erase_res = ErasePage(device, kFocalConfigSize, kRomCfgAddr);
  if (!erase_res) {
    FT_LOGE("Failed to erase ROM config page");
    return std::unexpected(erase_res.error());
  }

  auto reset_res = ResetMcu(device, kRomCfgAddr);
  if (!reset_res) {
    FT_LOGE("Failed to reset MCU to ROM");
    return std::unexpected(reset_res.error());
  }

  FT_LOGD("Device switched to ROM boot mode");
  return {};
}

std::expected<void, Error> GetBootVersion(const UsbDevice& device,
                                          DeviceMode current_mode) {
  auto cmd = BuildFtCmd(Opcode::kReadInfo, Subcmd::kReadData, 0, 0, 0, 0);

  const size_t buffer_size =
      (current_mode == DeviceMode::kBoot) ? kBootVersionMax : kRomVersionMax;

  constexpr size_t kMaxVersionBufferSize =
      std::max(kBootVersionMax, kRomVersionMax);
  std::array<uint8_t, kMaxVersionBufferSize> data{};

  auto res = ReceiveBulkDataIn(device, cmd, std::span(data).first(buffer_size),
                               kGenericInfoTimeout);
  if (!res) {
    FT_LOGE("Failed to read version info");
    return res;
  }

  auto null_pos = std::ranges::find(data, '\0');
  std::string version(data.begin(), null_pos);
  FT_LOGI("Boot version: {}", version);
  return {};
}

}  // namespace focaltech
