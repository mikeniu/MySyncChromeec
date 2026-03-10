/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "updater.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

// Third-party libraries come AFTER standard C++ libraries
#include <openssl/sha.h>

#include "ft_log.h"
#include "ft_scsi.h"
#include "usb_device.h"

namespace {

using namespace std::chrono_literals;

constexpr uint8_t kSoftPor = 0;

// Before bootloader v1.8, erasing the flash takes about 4.4 seconds.
constexpr auto kErasePageTimeout = 6000ms;
constexpr auto kDownloadPageTimeout = 1000ms;
constexpr auto kVerifyHashTimeout = 1000ms;
constexpr auto kMcuResetTimeout = 3000ms;
constexpr auto kGenericInfoTimeout = 200ms;

constexpr uint8_t kFtHashModeSha256 = 0x04;
constexpr uint8_t kFtHashModeRomShift = 4;

std::expected<std::vector<uint8_t>, FtError> ReadFileToVector(
    std::string_view path) {
  constexpr size_t kConfigPageSize = sizeof(FtConfigPage);
  std::ifstream file(std::string{path}, std::ios::binary | std::ios::ate);
  if (!file) {
    FT_LOGE("Cannot open file: {}", path);
    return std::unexpected(FtError::kNoSuchFile);
  }

  auto size = file.tellg();
  if (size < kConfigPageSize || size > kFtEcBinSizeMax) {
    FT_LOGE("Invalid file size: {}", static_cast<int64_t>(size));
    return std::unexpected(FtError::kParameterError);
  }

  std::vector<uint8_t> buffer(size);
  file.seekg(0, std::ios::beg);
  if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
    FT_LOGE("Failed to read file payload.");
    return std::unexpected(FtError::kResultFail);
  }

  return buffer;
}

std::array<uint8_t, SHA256_DIGEST_LENGTH> CalculateSha256(
    std::span<const uint8_t> data) {
  std::array<uint8_t, SHA256_DIGEST_LENGTH> hash{};
  SHA256(data.data(), data.size(), hash.data());
  return hash;
}

// For sending data to the MCU
std::expected<void, FtError> SendActionOut(
    const UsbDevice& device, uint8_t subcmd, uint32_t address,
    std::span<const uint8_t> data, TransferDirection dir,
    std::chrono::milliseconds timeout, uint32_t length = 0,
    uint32_t aux_address = 0, uint8_t flags = 0) {
  auto cmd =
      BuildFtCmd(kFtCmdAction, subcmd, address, length, aux_address, flags);
  return SendBulkDataOut(device, cmd, data, dir, timeout);
}

// For reading data from the MCU
std::expected<void, FtError> ReceiveActionIn(
    const UsbDevice& device, uint8_t subcmd, uint32_t address,
    std::span<uint8_t> data, std::chrono::milliseconds timeout,
    uint32_t length = 0, uint32_t aux_address = 0, uint8_t flags = 0) {
  auto cmd =
      BuildFtCmd(kFtCmdAction, subcmd, address, length, aux_address, flags);
  return ReceiveBulkDataIn(device, cmd, data, timeout);
}

std::expected<void, FtError> FtErasePage(const UsbDevice& device, size_t length,
                                         uint32_t addr) {
  uint32_t pages = FtDivRoundUp(length, kFocalConfigSize);
  return SendActionOut(device, kFtSubcmdErasePage, addr, {},
                       TransferDirection::kOut, kErasePageTimeout, pages);
}

std::expected<void, FtError> FtDownloadPage(const UsbDevice& device,
                                            uint32_t downaddr, size_t per_len,
                                            std::span<const uint8_t> data) {
  // Write data to SRAM (kFtSubcmdWriteBin)
  auto res = SendActionOut(device, kFtSubcmdWriteBin, kFtWriteSramAddr,
                           data.subspan(0, per_len), TransferDirection::kOut,
                           kDownloadPageTimeout, per_len);
  if (!res) {
    FT_LOGE("Write bin error");
    return res;
  }

  // Execute code
  res = SendActionOut(device, kFtSubcmdCodeBin, downaddr, {},
                      TransferDirection::kOut, kDownloadPageTimeout, per_len,
                      kFtWriteSramAddr);
  if (!res) FT_LOGE("Code bin error");
  return res;
}

std::expected<void, FtError> FtDownloadBin(const UsbDevice& device,
                                           std::string_view file_path,
                                           uint32_t target_addr) {
  auto file_data = ReadFileToVector(file_path);
  if (!file_data) return std::unexpected(file_data.error());

  const auto& buffer = *file_data;
  size_t file_len = buffer.size();
  size_t total_frame = FtDivRoundUp(file_len, kFocalConfigSize);

  // Erase config page area if needed
  if (target_addr != kFocalBootCfgAddr) {
    auto erase_res = FtErasePage(device, kFocalConfigSize, kFocalRomCfgAddr);
    if (!erase_res) return erase_res;
  }

  auto erase_res = FtErasePage(device, file_len, target_addr);
  if (!erase_res) return erase_res;

  size_t padded_buffer_size = total_frame * kFocalConfigSize;
  // 0xFF is the initial value of flash.
  constexpr uint8_t kFlashInitValue = 0xFF;
  std::vector<uint8_t> write_buffer(padded_buffer_size, kFlashInitValue);
  std::memcpy(write_buffer.data(), buffer.data(), file_len);

  uint8_t* p = write_buffer.data();
  uint32_t downaddr = target_addr;
  size_t per_len = kFocalConfigSize;
  [[maybe_unused]] size_t frame = 0;  // Used only in logging macros
  size_t len = file_len;              // Use exact file length for transmission

  if (target_addr == kFocalBootCfgAddr || target_addr == kFocalRomCfgAddr) {
    FT_LOGI("Write config page last");
    downaddr += per_len;
    p += per_len;
    len = (len > per_len) ? (len - per_len) : 0;
  }

  while (len >= per_len) {
    auto page_res = FtDownloadPage(device, downaddr, per_len, {p, per_len});
    if (!page_res) return page_res;

    len -= per_len;
    p += per_len;
    downaddr += per_len;
    FT_LOGD("Download frame {}/{}", ++frame, total_frame);
  }

  if (len > 0) {
    constexpr size_t kWordSize = sizeof(uint32_t);
    len = FtDivRoundUp<size_t>(len, kWordSize) *
          kWordSize;  // Align to 4-byte boundary correctly
    auto page_res = FtDownloadPage(device, downaddr, len, {p, len});
    if (!page_res) return page_res;
  }

  if (target_addr == kFocalBootCfgAddr || target_addr == kFocalRomCfgAddr) {
    FT_LOGI("Write config page");
    auto page_res = FtDownloadPage(device, target_addr, kFocalConfigSize,
                                   {write_buffer.data(), kFocalConfigSize});
    if (!page_res) return page_res;
    FT_LOGD("Download frame {}/{}", ++frame, total_frame);
  }

  return {};
}

std::expected<void, FtError> FtVerifyData(const UsbDevice& device,
                                          std::string_view file_path,
                                          uint32_t target_addr) {
  auto file_res = ReadFileToVector(file_path);
  if (!file_res.has_value()) {
    return std::unexpected(file_res.error());
  }
  const auto& firmware_data = file_res.value();

  // SHA256 mode, In ROM mode, this parameter needs to be left-shifted by 4
  // bits.
  uint8_t hash_mode = (target_addr == kFocalRomCfgAddr)
                          ? (kFtHashModeSha256 << kFtHashModeRomShift)
                          : kFtHashModeSha256;

  std::array<uint8_t, SHA256_DIGEST_LENGTH> device_hash{};

  auto io_res =
      ReceiveActionIn(device, kFtSubcmdVerify, target_addr, device_hash,
                      kVerifyHashTimeout, firmware_data.size(), 0, hash_mode);

  if (!io_res.has_value()) {
    FT_LOGE("Verification command failed over USB.");
    return std::unexpected(io_res.error());
  }

  auto local_hash = CalculateSha256(firmware_data);
  if (!std::ranges::equal(local_hash, device_hash)) {
    FT_LOGE("Hash mismatch. Local: {:02x}{:02x}... Device: {:02x}{:02x}...",
            local_hash[0], local_hash[1], device_hash[0], device_hash[1]);
    return std::unexpected(FtError::kResultFail);
  }

  FT_LOGD("Verification successful.");
  return {};
}

std::expected<void, FtError> FtGetBootVersion(const UsbDevice& device) {
  auto cmd = BuildFtCmd(kFtCmdReadInfo, kFtSubcmdReadData, 0, 0, 0, 0);
  std::vector<uint8_t> data(kFtBootVersionMax, 0);

  auto res = ReceiveBulkDataIn(device, cmd, data, kGenericInfoTimeout);
  if (!res) {
    FT_LOGE("Failed to read boot version");
    return res;
  }

  // Ensure null termination
  auto null_pos = std::ranges::find(data, '\0');
  std::string version(data.begin(), null_pos);
  FT_LOGI("Boot version: {}", version);
  return {};
}

// -----------------------------------------------------------------------------
// Helper to check if EC binary contains a valid config page
// -----------------------------------------------------------------------------
std::expected<bool, FtError> IsEcBinHasConfigPage(std::string_view file_path,
                                                  uint32_t& code_start_addr) {
  std::ifstream file(std::string{file_path}, std::ios::binary);
  if (!file) {
    FT_LOGE("Cannot open {}", file_path);
    return std::unexpected(FtError::kNoSuchFile);
  }

  FtConfigPage config{};
  file.read(reinterpret_cast<char*>(&config), sizeof(config));
  if (!file.good() && !file.eof()) {
    FT_LOGE("Failed to read config page");
    return std::unexpected(FtError::kResultFail);
  }

  if (config.code_valid_control_word != kCodeValidMagic) {
    return false;
  }

  if (file.gcount() != sizeof(config)) {
    FT_LOGE("Failed to read config page (file too small).");
    return std::unexpected(FtError::kResultFail);
  }

  code_start_addr = config.code_start_address;
  FT_LOGD("Config page found: start=0x{:x}", code_start_addr);
  return true;
}

std::expected<void, FtError> FtMcuReset(const UsbDevice& device,
                                        uint32_t target_addr) {
  if (target_addr != kFocalRomCfgAddr && target_addr != kFocalBootCfgAddr) {
    FT_LOGE("Reset address invalid: {:#x}", target_addr);
    return std::unexpected(FtError::kParameterError);
  }
  // Passing 0s for length and aux_address to reach the flags parameter
  return SendActionOut(device, kFtSubcmdSoftReset, 0, {},
                       TransferDirection::kSendOnly, kMcuResetTimeout, 0, 0,
                       kSoftPor);
}

}  // namespace

// -----------------------------------------------------------------------------
// Firmware update (public)
// -----------------------------------------------------------------------------
std::expected<void, FtError> UpdateFw(const UsbDevice& device,
                                      FtDeviceInfo& info) {
  uint32_t target_addr = kFocalRomCfgAddr;
  uint32_t code_start_addr = 0;

  auto abort_update = [&](FtError err) {
    if (target_addr == kFocalBootCfgAddr || target_addr == kFocalRomCfgAddr) {
      // Erase the config page so the device stays in its safe bootloader/rom
      // state
      FtErasePage(device, kFocalConfigSize, target_addr);
      FT_LOGW("Update aborted. Staying in {} mode",
              target_addr == kFocalBootCfgAddr ? "boot" : "rom");
    }
    return std::unexpected(err);
  };

  // Normal firmware update: check config page
  auto config_res = IsEcBinHasConfigPage(info.ec_bin, code_start_addr);
  if (!config_res) return std::unexpected(config_res.error());
  if (!*config_res) {
    FT_LOGE("Firmware lacks config page, cannot upgrade");
    return std::unexpected(FtError::kParameterError);
  }

  // Determine target address based on current mode
  if (info.current_mode == FtDeviceMode::kBoot) {
    target_addr = kFocalBootCfgAddr;
    if (code_start_addr != kFocalBootRunAddr) {
      FT_LOGE("EC binary not for bootloader mode");
      return std::unexpected(FtError::kParameterError);
    }
  } else if (info.current_mode == FtDeviceMode::kRom) {
    if (code_start_addr == kFocalBootRunAddr) {
      FT_LOGE("EC binary not for ROM mode");
      return std::unexpected(FtError::kParameterError);
    }
    if (code_start_addr != kFocalRomRunAddr) {
      FT_LOGE("EC binary start address mismatch");
      return std::unexpected(FtError::kParameterError);
    }
  } else {
    FT_LOGE("Invalid mode for update");
    return std::unexpected(FtError::kInvalidMode);
  }

  auto res = FtGetBootVersion(device);
  if (!res) {
    FT_LOGE("Get boot version failed");
    return abort_update(res.error());
  }

  if (info.ec_bin.empty()) {
    FT_LOGE("ec bin is null");
    return abort_update(FtError::kParameterError);
  }
  FT_LOGI("Begin firmware update: {}", info.ec_bin);
  // Download, verify, reset
  res = FtDownloadBin(device, info.ec_bin, target_addr);
  if (!res) {
    FT_LOGE("Download failed");
    return abort_update(res.error());
  }

  res = FtVerifyData(device, info.ec_bin, target_addr);
  if (!res) {
    FT_LOGE("Verification failed");
    return abort_update(res.error());
  }

  FT_LOGI("Firmware flashed successfully");

  res = FtMcuReset(device, target_addr);
  if (!res) {
    FT_LOGE("MCU reset failed");
    return abort_update(res.error());
  }

  return {};
}

// -----------------------------------------------------------------------------
// Get flash write-protect area from device (only valid in bootloader mode)
// -----------------------------------------------------------------------------
std::expected<FtFlashWpArea, FtError> FtGetFlashProtect(
    const UsbDevice& device, FtDeviceMode current_mode) {
  if (current_mode != FtDeviceMode::kBoot) {
    FT_LOGE("Get flash protect should only be done in boot mode");
    return std::unexpected(FtError::kInvalidMode);
  }

  FT_LOGI("Getting flash write protect area...");

  std::vector<uint8_t> data(
      kFtFlashWpBufferSize,
      0);  // Device returns 512 bytes, first byte holds area

  auto res = ReceiveActionIn(device, kFtSubcmdFlashWpGet, 0, data,
                             kGenericInfoTimeout);
  if (!res) {
    FT_LOGE("Failed to get flash protect status");
    return std::unexpected(res.error());
  }

  auto wp_area = static_cast<FtFlashWpArea>(data[0]);
  FT_LOGI("Flash write-protect area = {}", std::to_underlying(wp_area));
  return wp_area;
}

// -----------------------------------------------------------------------------
// Set flash write-protect area (only valid in bootloader mode)
// -----------------------------------------------------------------------------
std::expected<void, FtError> FtSetFlashProtect(const UsbDevice& device,
                                               FtFlashWpArea wp_area,
                                               FtDeviceMode current_mode) {
  if (current_mode != FtDeviceMode::kBoot) {
    FT_LOGE("Get flash protect should only be done in boot mode");
    return std::unexpected(FtError::kInvalidMode);
  }

  if (wp_area <= FtFlashWpArea::kUnknown ||
      wp_area >= FtFlashWpArea::kOutRange) {
    FT_LOGE("Invalid wp_area: {}", std::to_underlying(wp_area));
    return std::unexpected(FtError::kParameterError);
  }

  FT_LOGI("Setting flash write protect to area: {}",
          std::to_underlying(wp_area));

  std::vector<uint8_t> dummy(kFtFlashWpBufferSize, 0);

  auto res =
      ReceiveActionIn(device, kFtSubcmdFlashWpSet, std::to_underlying(wp_area),
                      dummy, kGenericInfoTimeout, ~std::to_underlying(wp_area));
  if (!res) {
    FT_LOGE("Failed to set flash protect");
    return std::unexpected(res.error());
  }

  // Verify the setting
  auto get_res = FtGetFlashProtect(device, current_mode);
  if (!get_res || *get_res != wp_area) {
    FT_LOGE("Flash protect verification failed");
    return std::unexpected(FtError::kResultFail);
  }

  FT_LOGI("Flash write-protect successfully set to area {}",
          std::to_underlying(wp_area));
  return {};
}

// -----------------------------------------------------------------------------
// Switch device back to ROM boot mode (erase config page and reset)
// -----------------------------------------------------------------------------
std::expected<void, FtError> FtBackToRomBoot(const UsbDevice& device,
                                             FtDeviceMode current_mode) {
  if (current_mode != FtDeviceMode::kBoot) {
    FT_LOGE("Current mode cannot switch to ROM via back command");
    return std::unexpected(FtError::kInvalidMode);
  }
  auto erase_res = FtErasePage(device, kFocalConfigSize, kFocalRomCfgAddr);
  if (!erase_res) {
    FT_LOGE("Failed to erase ROM config page");
    return std::unexpected(erase_res.error());
  }

  auto reset_res = FtMcuReset(device, kFocalRomCfgAddr);
  if (!reset_res) {
    FT_LOGE("Failed to reset MCU to ROM");
    return std::unexpected(reset_res.error());
  }

  FT_LOGD("Device switched to ROM boot mode");
  return {};
}
