/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "updater.h"

#include <openssl/sha.h>

#include <algorithm>
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
constexpr auto kMcuResetTimeout = 3000ms;
constexpr auto kGenericInfoTimeout = 200ms;

constexpr uint8_t kHashModeSha256 = 0x04;
constexpr uint8_t kHashModeRomShift = 4;

std::expected<std::vector<uint8_t>, Error> ReadFileToVector(
    std::string_view path) {
  constexpr size_t kConfigPageSize = sizeof(ConfigPage);
  std::ifstream file(std::string{path}, std::ios::binary | std::ios::ate);
  if (!file) {
    FT_LOGE("Cannot open file: {}", path);
    return std::unexpected(Error::kNoSuchFile);
  }

  auto size = file.tellg();
  if (size < kConfigPageSize || size > kEcBinSizeMax) {
    FT_LOGE("Invalid file size: {}", static_cast<int64_t>(size));
    return std::unexpected(Error::kParameterError);
  }

  std::vector<uint8_t> buffer(size);
  file.seekg(0, std::ios::beg);
  if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
    FT_LOGE("Failed to read file payload.");
    return std::unexpected(Error::kResultFail);
  }

  return buffer;
}

std::array<uint8_t, SHA256_DIGEST_LENGTH> CalculateSha256(
    std::span<const uint8_t> data) {
  std::array<uint8_t, SHA256_DIGEST_LENGTH> hash{};
  SHA256(data.data(), data.size(), hash.data());
  return hash;
}

std::expected<void, Error> SendActionOut(
    const UsbDevice& device, uint8_t subcmd, uint32_t address,
    std::span<const uint8_t> data, TransferDirection dir,
    std::chrono::milliseconds timeout, uint32_t length = 0,
    uint32_t aux_address = 0, uint8_t flags = 0) {
  auto cmd =
      BuildFtCmd(kCmdAction, subcmd, address, length, aux_address, flags);
  return SendBulkDataOut(device, cmd, data, dir, timeout);
}

std::expected<void, Error> ReceiveActionIn(
    const UsbDevice& device, uint8_t subcmd, uint32_t address,
    std::span<uint8_t> data, std::chrono::milliseconds timeout,
    uint32_t length = 0, uint32_t aux_address = 0, uint8_t flags = 0) {
  auto cmd =
      BuildFtCmd(kCmdAction, subcmd, address, length, aux_address, flags);
  return ReceiveBulkDataIn(device, cmd, data, timeout);
}

std::expected<void, Error> ErasePage(const UsbDevice& device, size_t length,
                                     uint32_t addr) {
  uint32_t pages = DivRoundUp(length, kFocalConfigSize);
  return SendActionOut(device, kSubcmdErasePage, addr, {},
                       TransferDirection::kOut, kErasePageTimeout, pages);
}

std::expected<void, Error> DownloadPage(const UsbDevice& device,
                                        uint32_t downaddr, size_t per_len,
                                        std::span<const uint8_t> data) {
  auto res = SendActionOut(device, kSubcmdWriteBin, kWriteSramAddr,
                           data.subspan(0, per_len), TransferDirection::kOut,
                           kDownloadPageTimeout, per_len);
  if (!res) {
    FT_LOGE("Write bin error");
    return res;
  }

  res = SendActionOut(device, kSubcmdCodeBin, downaddr, {},
                      TransferDirection::kOut, kDownloadPageTimeout, per_len,
                      kWriteSramAddr);
  if (!res) FT_LOGE("Code bin error");
  return res;
}

std::expected<void, Error> DownloadBin(const UsbDevice& device,
                                       std::string_view file_path,
                                       uint32_t target_addr) {
  auto file_data = ReadFileToVector(file_path);
  if (!file_data) return std::unexpected(file_data.error());

  const auto& buffer = *file_data;
  size_t file_len = buffer.size();

  if (target_addr != kBootCfgAddr) {
    auto erase_res = ErasePage(device, kFocalConfigSize, kRomCfgAddr);
    if (!erase_res) return erase_res;
  }

  auto erase_res = ErasePage(device, file_len, target_addr);
  if (!erase_res) return erase_res;

  std::span<const uint8_t> remaining_data = buffer;
  uint32_t downaddr = target_addr;

  if (target_addr == kBootCfgAddr || target_addr == kRomCfgAddr) {
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
    size_t padded_len = kFocalConfigSize;
    // 0xFF is the initial value of flash.
    std::vector<uint8_t> final_chunk(padded_len, 0xFF);
    std::ranges::copy(remaining_data, final_chunk.begin());

    auto page_res = DownloadPage(device, downaddr, padded_len, final_chunk);
    if (!page_res) return page_res;
  }

  if (target_addr == kBootCfgAddr || target_addr == kRomCfgAddr) {
    FT_LOGI("Write config page");
    auto page_res = DownloadPage(device, target_addr, kFocalConfigSize,
                                 {buffer.data(), kFocalConfigSize});
    if (!page_res) return page_res;
    FT_LOGD("Download frame {}/{}", ++frame, total_frame);
  }

  return {};
}

std::expected<void, Error> VerifyData(const UsbDevice& device,
                                      std::string_view file_path,
                                      uint32_t target_addr) {
  auto file_res = ReadFileToVector(file_path);
  if (!file_res.has_value()) {
    return std::unexpected(file_res.error());
  }
  const auto& firmware_data = file_res.value();

  // SHA256 mode, In ROM mode, this parameter needs to be left-shifted by 4
  // bits.
  uint8_t hash_mode = (target_addr == kRomCfgAddr)
                          ? (kHashModeSha256 << kHashModeRomShift)
                          : kHashModeSha256;

  std::array<uint8_t, SHA256_DIGEST_LENGTH> device_hash{};

  auto io_res =
      ReceiveActionIn(device, kSubcmdVerify, target_addr, device_hash,
                      kVerifyHashTimeout, firmware_data.size(), 0, hash_mode);

  if (!io_res.has_value()) {
    FT_LOGE("Verification command failed over USB.");
    return std::unexpected(io_res.error());
  }

  auto local_hash = CalculateSha256(firmware_data);
  if (!std::ranges::equal(local_hash, device_hash)) {
    FT_LOGE("Hash mismatch. Local: {:02x}{:02x}... Device: {:02x}{:02x}...",
            local_hash[0], local_hash[1], device_hash[0], device_hash[1]);
    return std::unexpected(Error::kResultFail);
  }

  FT_LOGD("Verification successful.");
  return {};
}

std::expected<void, Error> GetBootVersion(const UsbDevice& device) {
  auto cmd = BuildFtCmd(kCmdReadInfo, kSubcmdReadData, 0, 0, 0, 0);
  std::vector<uint8_t> data(kBootVersionMax, 0);

  auto res = ReceiveBulkDataIn(device, cmd, data, kGenericInfoTimeout);
  if (!res) {
    FT_LOGE("Failed to read boot version");
    return res;
  }

  auto null_pos = std::ranges::find(data, '\0');
  std::string version(data.begin(), null_pos);
  FT_LOGI("Boot version: {}", version);
  return {};
}

std::expected<bool, Error> HasValidConfigPage(std::string_view file_path,
                                              uint32_t& code_start_addr) {
  std::ifstream file(std::string{file_path}, std::ios::binary);
  if (!file) {
    FT_LOGE("Cannot open {}", file_path);
    return std::unexpected(Error::kNoSuchFile);
  }

  ConfigPage config{};
  file.read(reinterpret_cast<char*>(&config), sizeof(config));
  if (!file.good() && !file.eof()) {
    FT_LOGE("Failed to read config page");
    return std::unexpected(Error::kResultFail);
  }

  if (file.gcount() != sizeof(config)) {
    FT_LOGE("Failed to read config page (file too small).");
    return std::unexpected(Error::kResultFail);
  }

  if (config.code_valid_control_word != kCodeValidMagic) {
    return false;
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
  // Passing 0s for length and aux_address to reach the flags parameter
  return SendActionOut(device, kSubcmdSoftReset, 0, {},
                       TransferDirection::kSendOnly, kMcuResetTimeout, 0, 0,
                       kSoftPor);
}

}  // namespace

std::expected<void, Error> UpdateFw(const UsbDevice& device,
                                    std::string_view ec_bin,
                                    DeviceMode current_mode) {
  uint32_t target_addr = kRomCfgAddr;
  uint32_t code_start_addr = 0;

  auto abort_update = [&](Error err) {
    if (target_addr == kBootCfgAddr || target_addr == kRomCfgAddr) {
      ErasePage(device, kFocalConfigSize, target_addr);
      FT_LOGW("Update aborted. Staying in {} mode",
              target_addr == kBootCfgAddr ? "boot" : "rom");
    }
    return std::unexpected(err);
  };

  auto config_res = HasValidConfigPage(ec_bin, code_start_addr);
  if (!config_res) return std::unexpected(config_res.error());
  if (!*config_res) {
    FT_LOGE("Firmware lacks config page, cannot upgrade");
    return std::unexpected(Error::kParameterError);
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

  auto res = GetBootVersion(device);
  if (!res) {
    FT_LOGE("Get boot version failed");
    return abort_update(res.error());
  }

  if (ec_bin.empty()) {
    FT_LOGE("ec bin is null");
    return abort_update(Error::kParameterError);
  }
  FT_LOGI("Begin firmware update: {}", ec_bin);
  res = DownloadBin(device, ec_bin, target_addr);
  if (!res) {
    FT_LOGE("Download failed");
    return abort_update(res.error());
  }

  res = VerifyData(device, ec_bin, target_addr);
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

  std::vector<uint8_t> data(kFlashWpBufferSize, 0);

  auto res =
      ReceiveActionIn(device, kSubcmdFlashWpGet, 0, data, kGenericInfoTimeout);
  if (!res) {
    FT_LOGE("Failed to get flash protect status");
    return std::unexpected(res.error());
  }

  auto wp_area = static_cast<FlashWpArea>(data[0]);
  FT_LOGI("Flash write-protect area = {}", std::to_underlying(wp_area));
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
    FT_LOGE("Invalid wp_area: {}", std::to_underlying(wp_area));
    return std::unexpected(Error::kParameterError);
  }

  FT_LOGI("Setting flash write protect to area: {}",
          std::to_underlying(wp_area));

  std::vector<uint8_t> dummy(kFlashWpBufferSize, 0);

  auto res =
      ReceiveActionIn(device, kSubcmdFlashWpSet, std::to_underlying(wp_area),
                      dummy, kGenericInfoTimeout, ~std::to_underlying(wp_area));
  if (!res) {
    FT_LOGE("Failed to set flash protect");
    return std::unexpected(res.error());
  }

  auto get_res = GetFlashProtection(device, current_mode);
  if (!get_res || *get_res != wp_area) {
    FT_LOGE("Flash protect verification failed");
    return std::unexpected(Error::kResultFail);
  }

  FT_LOGI("Flash write-protect successfully set to area {}",
          std::to_underlying(wp_area));
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

}  // namespace focaltech
