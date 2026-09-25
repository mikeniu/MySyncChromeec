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
  return (addr == kRomConfigAddr || addr == kBootConfigAddr);
}

std::expected<void, Error> ErasePage(const UsbDevice& device,
                                     size_t length_bytes, uint32_t address) {
  const uint32_t page_count =
      static_cast<uint32_t>(DivRoundUp(length_bytes, kFocalConfigSize));
  return SendBulkDataOut(device,
                         CmdFormat{
                             .opcode = Opcode::kAction,
                             .address = ToLittleEndian(address),
                             .length = ToLittleEndian(page_count),
                             .subcmd = Subcmd::kErasePage,
                         },
                         {}, TransferDirection::kOut, kErasePageTimeout);
}

std::expected<void, Error> DownloadPage(const UsbDevice& device,
                                        uint32_t target_address,
                                        std::span<const uint8_t> chunk) {
  const auto sram_write_status = SendBulkDataOut(
      device,
      CmdFormat{
          .opcode = Opcode::kAction,
          .address = ToLittleEndian(kWriteSramAddr),
          .length = ToLittleEndian(static_cast<uint32_t>(chunk.size())),
          .subcmd = Subcmd::kWriteBin,
      },
      chunk, TransferDirection::kOut, kDownloadPageTimeout);
  if (!sram_write_status) {
    FT_LOGE("Write bin error");
    return sram_write_status;
  }

  const auto flash_commit_status = SendBulkDataOut(
      device,
      CmdFormat{
          .opcode = Opcode::kAction,
          .address = ToLittleEndian(target_address),
          .length = ToLittleEndian(static_cast<uint32_t>(chunk.size())),
          .aux_address = ToLittleEndian(kWriteSramAddr),
          .subcmd = Subcmd::kCodeBin,
      },
      {}, TransferDirection::kOut, kDownloadPageTimeout);
  if (!flash_commit_status) FT_LOGE("Code bin error");
  return flash_commit_status;
}

std::expected<void, Error> DownloadBin(const UsbDevice& device,
                                       std::span<const uint8_t> buffer,
                                       uint32_t target_address) {
  const bool is_config_update = IsConfigAddress(target_address);
  const size_t file_length = buffer.size();

  if (!is_config_update) {
    const auto erase_status =
        ErasePage(device, kFocalConfigSize, kRomConfigAddr);
    if (!erase_status) return erase_status;
  }

  const auto erase_status = ErasePage(device, file_length, target_address);
  if (!erase_status) return erase_status;

  std::span<const uint8_t> remaining_data = buffer;
  uint32_t current_address = target_address;

  if (is_config_update) {
    FT_LOGI("Write config page last");
    remaining_data = remaining_data.subspan(
        std::min(remaining_data.size(), kFocalConfigSize));
    current_address += kFocalConfigSize;
  }

  [[maybe_unused]] size_t current_page = 0;
  [[maybe_unused]] size_t total_pages =
      DivRoundUp(file_length, kFocalConfigSize);
  while (remaining_data.size() >= kFocalConfigSize) {
    const auto page_status = DownloadPage(
        device, current_address, remaining_data.first(kFocalConfigSize));
    if (!page_status) return page_status;

    remaining_data = remaining_data.subspan(kFocalConfigSize);
    current_address += kFocalConfigSize;
    FT_LOGD("Download page {}/{}", ++current_page, total_pages);
  }

  if (!remaining_data.empty()) {
    std::array<uint8_t, kFocalConfigSize> final_chunk;
    constexpr uint8_t kErasedFlashByte = 0xFF;
    final_chunk.fill(kErasedFlashByte);
    std::ranges::copy(remaining_data, final_chunk.begin());

    const auto page_status = DownloadPage(device, current_address, final_chunk);
    if (!page_status) return page_status;
  }

  if (is_config_update) {
    FT_LOGI("Write config page");
    const auto page_status =
        DownloadPage(device, target_address, buffer.first(kFocalConfigSize));
    if (!page_status) return page_status;
    FT_LOGD("Download page {}/{}", ++current_page, total_pages);
  }

  return {};
}

std::expected<void, Error> VerifyData(const UsbDevice& device,
                                      std::span<const uint8_t> firmware_data,
                                      uint32_t target_address) {
  const uint8_t hash_mode = (target_address == kRomConfigAddr)
                                ? (kHashModeSha256 << kHashModeRomShift)
                                : kHashModeSha256;

  std::array<uint8_t, SHA256_DIGEST_LENGTH> device_hash{};

  const auto verify_io_result = ReceiveBulkDataIn(
      device,
      CmdFormat{
          .opcode = Opcode::kAction,
          .address = ToLittleEndian(target_address),
          .length = ToLittleEndian(static_cast<uint32_t>(firmware_data.size())),
          .subcmd = Subcmd::kVerify,
          .flags = hash_mode,
      },
      device_hash, kVerifyHashTimeout);

  if (!verify_io_result) {
    FT_LOGE("Verification command failed over USB.");
    return std::unexpected(verify_io_result.error());
  }

  if (*verify_io_result != SHA256_DIGEST_LENGTH) {
    FT_LOGE("Short read during verification. Expected {}, got {}",
            SHA256_DIGEST_LENGTH, *verify_io_result);
    return std::unexpected(Error::kHardwareFailure);
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
    std::span<const uint8_t> firmware_data) {
  if (firmware_data.size() < sizeof(ConfigPage)) {
    FT_LOGE("Firmware data too small for config page.");
    return std::unexpected(Error::kHardwareFailure);
  }

  ConfigPage config{};
  std::memcpy(&config, firmware_data.data(), sizeof(config));

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
                                    uint32_t target_address) {
  if (!IsConfigAddress(target_address)) {
    FT_LOGE("Reset address invalid: {:#x}", target_address);
    return std::unexpected(Error::kInvalidParameter);
  }

  const auto reset_cmd_status =
      SendBulkDataOut(device,
                      CmdFormat{
                          .opcode = Opcode::kAction,
                          .subcmd = Subcmd::kSoftReset,
                          .flags = kSoftPor,
                      },
                      {}, TransferDirection::kSendOnly, kDeviceRebootTimeout);

  if (!reset_cmd_status) {
    return std::unexpected(Error::kHardwareFailure);
  }

  return {};
}

}  // namespace

std::expected<void, Error> UpdateFirmware(
    const UsbDevice& device, std::span<const uint8_t> firmware_data,
    DeviceMode current_mode) {
  if (firmware_data.size() > kFirmwareBinSizeMax) {
    FT_LOGE("Firmware file exceeds maximum allowed size.");
    return std::unexpected(Error::kInvalidParameter);
  }

  const auto code_start_result = GetCodeStartAddress(firmware_data);
  if (!code_start_result) {
    return std::unexpected(code_start_result.error());
  }
  const uint32_t firmware_run_address = *code_start_result;

  const auto memory_map_result = GetMemoryMap(current_mode);
  if (!memory_map_result) {
    FT_LOGE("Invalid device mode for update");
    return std::unexpected(memory_map_result.error());
  }
  const MemoryMap hw_map = *memory_map_result;

  if (firmware_run_address != hw_map.run_address) {
    FT_LOGE(
        "Firmware run address (0x{:x}) does not match current mode run address "
        "(0x{:x}).",
        firmware_run_address, hw_map.run_address);
    return std::unexpected(Error::kInvalidParameter);
  }

  const uint32_t config_address = hw_map.config_address;

  auto abort_update = [&](Error err) {
    if (IsConfigAddress(config_address)) {
      ErasePage(device, kFocalConfigSize, config_address);
      FT_LOGW("Update aborted. Staying in {} mode",
              current_mode == DeviceMode::kBootloader ? "bootloader" : "rom");
    }
    return std::unexpected(err);
  };

  const auto version_check_result = GetBootVersion(device, current_mode);
  if (!version_check_result) {
    FT_LOGE("Get boot version failed");
    return abort_update(version_check_result.error());
  }

  FT_LOGI("Begin firmware update");
  const auto download_status =
      DownloadBin(device, firmware_data, config_address);
  if (!download_status) {
    FT_LOGE("Download failed");
    return abort_update(download_status.error());
  }

  const auto verification_result =
      VerifyData(device, firmware_data, config_address);
  if (!verification_result) {
    FT_LOGE("Verification failed");
    return abort_update(verification_result.error());
  }

  FT_LOGI("Firmware flashed successfully");

  const auto reset_result = ResetMcu(device, config_address);
  if (!reset_result) {
    FT_LOGE("MCU reset failed");
    return abort_update(reset_result.error());
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

  std::array<uint8_t, kFlashWpBufferSize> status_buffer{};

  const auto read_status = ReceiveBulkDataIn(
      device,
      CmdFormat{.opcode = Opcode::kAction, .subcmd = Subcmd::kFlashWpGet},
      status_buffer, kGenericInfoTimeout);

  if (!read_status) {
    FT_LOGE("Failed to get flash protect status");
    return std::unexpected(read_status.error());
  }

  if (*read_status < 1) {
    FT_LOGE("Received 0 bytes for flash protect status");
    return std::unexpected(Error::kHardwareFailure);
  }

  const auto wp_area = static_cast<FlashWpArea>(status_buffer[0]);
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

  if (wp_area <= FlashWpArea::kUnknown || wp_area >= FlashWpArea::kOutOfRange) {
    FT_LOGE("Invalid wp_area: {}", ToString(wp_area));
    return std::unexpected(Error::kInvalidParameter);
  }

  FT_LOGI("Setting flash write protect to area: {}", ToString(wp_area));

  std::array<uint8_t, kFlashWpBufferSize> ignored_response{};

  const auto set_status = ReceiveBulkDataIn(
      device,
      CmdFormat{
          .opcode = Opcode::kAction,
          .subcmd = Subcmd::kFlashWpSet,
          .address = ToLittleEndian(std::to_underlying(wp_area)),
          .length = ToLittleEndian(~std::to_underlying(wp_area)),
      },
      ignored_response, kGenericInfoTimeout);

  if (!set_status) {
    FT_LOGE("Failed to set flash protect");
    return std::unexpected(set_status.error());
  }

  const auto verify_status = GetFlashProtection(device, current_mode);
  if (!verify_status || *verify_status != wp_area) {
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

  const auto erase_status = ErasePage(device, kFocalConfigSize, kRomConfigAddr);
  if (!erase_status) {
    FT_LOGE("Failed to erase ROM config page");
    return std::unexpected(erase_status.error());
  }

  const auto reset_status = ResetMcu(device, kRomConfigAddr);
  if (!reset_status) {
    FT_LOGE("Failed to reset MCU to ROM");
    return std::unexpected(reset_status.error());
  }

  FT_LOGD("Device switched to ROM boot mode");
  return {};
}

std::expected<void, Error> GetBootVersion(const UsbDevice& device,
                                          DeviceMode current_mode) {
  const size_t expected_size = (current_mode == DeviceMode::kBootloader)
                                   ? kBootVersionMax
                                   : kRomVersionMax;

  std::array<uint8_t, std::max(kBootVersionMax, kRomVersionMax)>
      version_buffer{};
  auto buffer = std::span(version_buffer).first(expected_size);

  const auto read_result = ReceiveBulkDataIn(
      device,
      CmdFormat{.opcode = Opcode::kReadInfo, .subcmd = Subcmd::kReadData},
      buffer, kGenericInfoTimeout);

  if (!read_result) {
    FT_LOGE("Failed to read version info");
    return std::unexpected(read_result.error());
  }

  auto valid_data = buffer.first(*read_result);
  const auto null_terminator_it = std::ranges::find(valid_data, '\0');
  const std::string version(valid_data.begin(), null_terminator_it);

  FT_LOGI("Boot version: {}", version);
  return {};
}

}  // namespace focaltech
