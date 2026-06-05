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

constexpr bool IsConfigAddress(uint32_t addr) {
  return (addr == kRomCfgAddr || addr == kBootCfgAddr);
}

std::expected<void, Error> ErasePage(const UsbDevice& device, size_t length,
                                     uint32_t addr) {
  const uint32_t pages =
      static_cast<uint32_t>(DivRoundUp(length, kFocalConfigSize));
  return SendBulkDataOut(device,
                         CmdFormat{
                             .opcode = Opcode::kAction,
                             .address = ToLittleEndian(addr),
                             .length = ToLittleEndian(pages),
                             .subcmd = Subcmd::kErasePage,
                         },
                         {}, TransferDirection::kOut, kErasePageTimeout);
}

std::expected<void, Error> DownloadPage(const UsbDevice& device,
                                        uint32_t target_address,
                                        std::span<const uint8_t> chunk) {
  const auto res = SendBulkDataOut(
      device,
      CmdFormat{
          .opcode = Opcode::kAction,
          .address = ToLittleEndian(kWriteSramAddr),
          .length = ToLittleEndian(static_cast<uint32_t>(chunk.size())),
          .subcmd = Subcmd::kWriteBin,
      },
      chunk, TransferDirection::kOut, kDownloadPageTimeout);
  if (!res) {
    FT_LOGE("Write bin error");
    return res;
  }

  const auto res2 = SendBulkDataOut(
      device,
      CmdFormat{
          .opcode = Opcode::kAction,
          .address = ToLittleEndian(target_address),
          .length = ToLittleEndian(static_cast<uint32_t>(chunk.size())),
          .aux_address = ToLittleEndian(kWriteSramAddr),
          .subcmd = Subcmd::kCodeBin,
      },
      {}, TransferDirection::kOut, kDownloadPageTimeout);
  if (!res2) FT_LOGE("Code bin error");
  return res2;
}

std::expected<void, Error> DownloadBin(const UsbDevice& device,
                                       std::span<const uint8_t> buffer,
                                       uint32_t target_addr) {
  const bool is_config_update = IsConfigAddress(target_addr);

  const size_t file_len = buffer.size();

  if (!is_config_update) {
    const auto erase_res = ErasePage(device, kFocalConfigSize, kRomCfgAddr);
    if (!erase_res) return erase_res;
  }

  const auto erase_res = ErasePage(device, file_len, target_addr);
  if (!erase_res) return erase_res;

  std::span<const uint8_t> remaining_data = buffer;
  uint32_t target_address = target_addr;

  if (is_config_update) {
    FT_LOGI("Write config page last");
    remaining_data = remaining_data.subspan(
        std::min(remaining_data.size(), kFocalConfigSize));
    target_address += kFocalConfigSize;
  }

  [[maybe_unused]] size_t frame = 0;
  [[maybe_unused]] size_t total_frame = DivRoundUp(file_len, kFocalConfigSize);
  while (remaining_data.size() >= kFocalConfigSize) {
    const auto page_res = DownloadPage(device, target_address,
                                       remaining_data.first(kFocalConfigSize));
    if (!page_res) return page_res;

    remaining_data = remaining_data.subspan(kFocalConfigSize);
    target_address += kFocalConfigSize;
    FT_LOGD("Download frame {}/{}", ++frame, total_frame);
  }

  if (!remaining_data.empty()) {
    std::array<uint8_t, kFocalConfigSize> final_chunk;
    constexpr uint8_t kErasedFlashByte = 0xFF;
    final_chunk.fill(kErasedFlashByte);
    std::ranges::copy(remaining_data, final_chunk.begin());

    const auto page_res = DownloadPage(device, target_address, final_chunk);
    if (!page_res) return page_res;
  }

  if (is_config_update) {
    FT_LOGI("Write config page");
    const auto page_res =
        DownloadPage(device, target_addr, buffer.first(kFocalConfigSize));
    if (!page_res) return page_res;
    FT_LOGD("Download frame {}/{}", ++frame, total_frame);
  }

  return {};
}

std::expected<void, Error> VerifyData(const UsbDevice& device,
                                      std::span<const uint8_t> firmware_data,
                                      uint32_t target_addr) {
  const uint8_t hash_mode = (target_addr == kRomCfgAddr)
                                ? (kHashModeSha256 << kHashModeRomShift)
                                : kHashModeSha256;

  std::array<uint8_t, SHA256_DIGEST_LENGTH> device_hash{};

  const auto io_res = ReceiveBulkDataIn(
      device,
      CmdFormat{
          .opcode = Opcode::kAction,
          .address = ToLittleEndian(target_addr),
          .length = ToLittleEndian(static_cast<uint32_t>(firmware_data.size())),
          .subcmd = Subcmd::kVerify,
          .flags = hash_mode,
      },
      device_hash, kVerifyHashTimeout);

  if (!io_res.has_value()) {
    FT_LOGE("Verification command failed over USB.");
    return std::unexpected(io_res.error());
  }

  const auto local_hash = CalculateSha256(firmware_data);
  if (local_hash != device_hash) {
    FT_LOGE("Hash mismatch. Local: {:02x}{:02x}... Device: {:02x}{:02x}...",
            local_hash[0], local_hash[1], device_hash[0], device_hash[1]);
    return std::unexpected(Error::kVerificationFailed);
  }

  FT_LOGD("Verification successful.");
  return {};
}

std::expected<uint32_t, Error> GetCodeStartAddress(
    std::span<const uint8_t> fw_data) {
  if (fw_data.size() < sizeof(ConfigPage)) {
    FT_LOGE("Firmware data too small for config page.");
    return std::unexpected(Error::kHardwareFailure);
  }

  ConfigPage config{};
  std::memcpy(&config, fw_data.data(), sizeof(config));

  if (config.code_valid_control_word != kCodeValidMagic) {
    FT_LOGE(
        "Invalid magic number in config page (expected 0x{:08x}, got 0x{:08x})",
        kCodeValidMagic, config.code_valid_control_word);
    return std::unexpected(Error::kInvalidFormat);
  }

  FT_LOGD("Config page found: start=0x{:x}", config.code_start_address);
  return config.code_start_address;
}

std::expected<void, Error> ResetMcu(const UsbDevice& device,
                                    uint32_t target_addr) {
  if (!IsConfigAddress(target_addr)) {
    FT_LOGE("Reset address invalid: {:#x}", target_addr);
    return std::unexpected(Error::kInvalidParameter);
  }

  const auto res =
      SendBulkDataOut(device,
                      CmdFormat{
                          .opcode = Opcode::kAction,
                          .subcmd = Subcmd::kSoftReset,
                          .flags = kSoftPor,
                      },
                      {}, TransferDirection::kSendOnly, kDeviceRebootTimeout);

  if (!res) {
    return std::unexpected(Error::kHardwareFailure);
  }

  return {};
}

}  // namespace

std::expected<void, Error> UpdateFw(const UsbDevice& device,
                                    std::span<const uint8_t> fw_data,
                                    DeviceMode current_mode) {
  if (fw_data.size() > kFwBinSizeMax) {
    FT_LOGE("Firmware file exceeds maximum allowed size.");
    return std::unexpected(Error::kInvalidParameter);
  }

  const auto start_addr_res = GetCodeStartAddress(fw_data);
  if (!start_addr_res) {
    return std::unexpected(start_addr_res.error());
  }
  const uint32_t fw_run_address = *start_addr_res;

  const auto map_res = GetMemoryMap(current_mode);
  if (!map_res) {
    FT_LOGE("Invalid device mode for update");
    return std::unexpected(map_res.error());
  }
  const MemoryMap hw_map = *map_res;

  if (fw_run_address != hw_map.run_address) {
    FT_LOGE(
        "Firmware run address (0x{:x}) does not match current mode run address "
        "(0x{:x}).",
        fw_run_address, hw_map.run_address);
    return std::unexpected(Error::kInvalidParameter);
  }

  const uint32_t target_addr = hw_map.config_address;

  auto abort_update = [&](Error err) {
    if (IsConfigAddress(target_addr)) {
      ErasePage(device, kFocalConfigSize, target_addr);
      FT_LOGW("Update aborted. Staying in {} mode",
              current_mode == DeviceMode::kBootloader ? "bootloader" : "rom");
    }
    return std::unexpected(err);
  };

  const auto version_res = GetBootVersion(device, current_mode);
  if (!version_res) {
    FT_LOGE("Get boot version failed");
    return abort_update(version_res.error());
  }

  FT_LOGI("Begin firmware update");
  const auto download_res = DownloadBin(device, fw_data, target_addr);
  if (!download_res) {
    FT_LOGE("Download failed");
    return abort_update(download_res.error());
  }

  const auto verify_res = VerifyData(device, fw_data, target_addr);
  if (!verify_res) {
    FT_LOGE("Verification failed");
    return abort_update(verify_res.error());
  }

  FT_LOGI("Firmware flashed successfully");

  const auto reset_res = ResetMcu(device, target_addr);
  if (!reset_res) {
    FT_LOGE("MCU reset failed");
    return abort_update(reset_res.error());
  }

  return {};
}

std::expected<FlashWpArea, Error> GetFlashProtection(const UsbDevice& device,
                                                     DeviceMode current_mode) {
  if (current_mode != DeviceMode::kBootloader) {
    FT_LOGE("Get flash protect should only be done in bootloader mode");
    return std::unexpected(Error::kInvalidMode);
  }

  FT_LOGI("Getting flash write protect area...");

  std::array<uint8_t, kFlashWpBufferSize> data{};

  const auto res = ReceiveBulkDataIn(device,
                                     CmdFormat{
                                         .opcode = Opcode::kAction,
                                         .subcmd = Subcmd::kFlashWpGet,
                                     },
                                     data, kGenericInfoTimeout);
  if (!res) {
    FT_LOGE("Failed to get flash protect status");
    return std::unexpected(res.error());
  }

  const auto wp_area = static_cast<FlashWpArea>(data[0]);
  FT_LOGI("Flash write-protect area = {}", ToString(wp_area));
  return wp_area;
}

std::expected<void, Error> SetFlashProtection(const UsbDevice& device,
                                              FlashWpArea wp_area,
                                              DeviceMode current_mode) {
  if (current_mode != DeviceMode::kBootloader) {
    FT_LOGE("Set flash protect should only be done in bootloader mode");
    return std::unexpected(Error::kInvalidMode);
  }

  if (wp_area <= FlashWpArea::kUnknown || wp_area >= FlashWpArea::kOutRange) {
    FT_LOGE("Invalid wp_area: {}", ToString(wp_area));
    return std::unexpected(Error::kInvalidParameter);
  }

  FT_LOGI("Setting flash write protect to area: {}", ToString(wp_area));

  std::array<uint8_t, kFlashWpBufferSize> ignored_response{};

  const auto res = ReceiveBulkDataIn(
      device,
      CmdFormat{
          .opcode = Opcode::kAction,
          .subcmd = Subcmd::kFlashWpSet,
          .address = ToLittleEndian(std::to_underlying(wp_area)),
          .length = ToLittleEndian(~std::to_underlying(wp_area)),
      },
      ignored_response, kGenericInfoTimeout);
  if (!res) {
    FT_LOGE("Failed to set flash protect");
    return std::unexpected(res.error());
  }

  const auto get_res = GetFlashProtection(device, current_mode);
  if (!get_res || *get_res != wp_area) {
    FT_LOGE("Flash protect verification failed");
    return std::unexpected(Error::kHardwareFailure);
  }

  FT_LOGI("Flash write-protect successfully set to area {}", ToString(wp_area));
  return {};
}

std::expected<void, Error> ReturnToRomBoot(const UsbDevice& device,
                                           DeviceMode current_mode) {
  if (current_mode != DeviceMode::kBootloader) {
    FT_LOGE("Current mode cannot switch to ROM via back command");
    return std::unexpected(Error::kInvalidMode);
  }

  const auto erase_res = ErasePage(device, kFocalConfigSize, kRomCfgAddr);
  if (!erase_res) {
    FT_LOGE("Failed to erase ROM config page");
    return std::unexpected(erase_res.error());
  }

  const auto reset_res = ResetMcu(device, kRomCfgAddr);
  if (!reset_res) {
    FT_LOGE("Failed to reset MCU to ROM");
    return std::unexpected(reset_res.error());
  }

  FT_LOGD("Device switched to ROM boot mode");
  return {};
}

std::expected<void, Error> GetBootVersion(const UsbDevice& device,
                                          DeviceMode current_mode) {
  const CmdFormat cmd{
      .opcode = Opcode::kReadInfo,
      .subcmd = Subcmd::kReadData,
  };

  const size_t buffer_size = (current_mode == DeviceMode::kBootloader)
                                 ? kBootVersionMax
                                 : kRomVersionMax;

  constexpr size_t kMaxVersionBufferSize =
      std::max(kBootVersionMax, kRomVersionMax);
  std::array<uint8_t, kMaxVersionBufferSize> data{};

  const auto res = ReceiveBulkDataIn(
      device, cmd, std::span(data).first(buffer_size), kGenericInfoTimeout);
  if (!res) {
    FT_LOGE("Failed to read version info");
    return res;
  }

  const auto null_pos = std::ranges::find(data, '\0');
  const std::string version(data.begin(), null_pos);
  FT_LOGI("Boot version: {}", version);
  return {};
}

}  // namespace focaltech
